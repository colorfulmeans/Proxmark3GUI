#include <QtTest>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "offlinepolicy.h"
#include "pm3process.h"

namespace {

QString fixtureName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("pm3fixture.exe");
#else
    return QStringLiteral("pm3fixture");
#endif
}

QByteArray digest(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return QByteArray();
    return hash.result().toHex();
}

struct Fixture
{
    QTemporaryDir temporary;
    QString directory;
    QString executable;
    QByteArray expectedHash;
    QString error;

    bool prepare(const QString &mode = QString())
    {
        if (!temporary.isValid()) {
            error = QStringLiteral("Cannot create fixture directory.");
            return false;
        }
        directory = QFileInfo(temporary.path()).canonicalFilePath();
        const QString source = QDir(QCoreApplication::applicationDirPath()).filePath(fixtureName());
        executable = QDir(directory).filePath(fixtureName());
        if (!QFile::copy(source, executable)) {
            error = QStringLiteral("Cannot copy the compiled native helper: ") + source;
            return false;
        }
        if (!OfflinePolicy::validateExecutablePath(executable, nullptr, &error))
            return false;
        expectedHash = digest(executable);
        if (expectedHash.size() != 64)
            return false;
        if (!mode.isEmpty()) {
            QFile file(QDir(directory).filePath(QStringLiteral("fixture-mode.txt")));
            if (!file.open(QIODevice::WriteOnly) || file.write(mode.toUtf8()) < 0)
                return false;
        }
        return true;
    }

    void launch(PM3Process &process) const
    {
        process.setWorkingDir(directory);
        process.connectPM3(executable, QStringList{QStringLiteral("COM3"), QStringLiteral("-f")}, expectedHash);
    }

    int starts() const
    {
        QFile file(QDir(directory).filePath(QStringLiteral("fixture-started.txt")));
        if (!file.open(QIODevice::ReadOnly))
            return 0;
        return file.readAll().count("started");
    }
};

QString outputText(const QSignalSpy &spy)
{
    QString result;
    for (const QList<QVariant> &call : spy)
        result += call.at(0).toString();
    return result;
}

bool reportedState(const QSignalSpy &spy, bool expected)
{
    for (const QList<QVariant> &call : spy)
        if (call.at(0).toBool() == expected)
            return true;
    return false;
}

QString received(const QString &command)
{
    return QStringLiteral("RX:") + QString::fromLatin1(command.toUtf8().toHex()) + QLatin1Char('\n');
}

} // namespace

class ProcessBoundaryTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void startsNativeBackendAndForwardsCommands();
    void blockedCommandsNeverReachBackend();
    void checksumMismatchNeverStarts();
    void invalidArgumentsNeverStart();
    void invalidWorkspaceNeverStarts();
    void reconnectRechecksChecksum();
    void reconnectUnchangedBackend();
    void serialListenerCanBeReenabled();
    void unexpectedExitReportsDisconnected();
    void startupExitNeverReportsConnected();
    void handshakeAcrossChunks_data();
    void handshakeAcrossChunks();
    void writeWhileDisconnectedIsBlocked();
    void utilRejectsBeforeEmitting();
};

void ProcessBoundaryTest::initTestCase()
{
    qRegisterMetaType<Util::ClientType>("Util::ClientType");
    const QString helper = QDir(QCoreApplication::applicationDirPath()).filePath(fixtureName());
    QString error;
    QVERIFY2(OfflinePolicy::validateExecutablePath(helper, nullptr, &error), qPrintable(error));
}

void ProcessBoundaryTest::startsNativeBackendAndForwardsCommands()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(), qPrintable(fixture.error));
    PM3Process process(nullptr);
    QSignalSpy state(&process, &PM3Process::PM3StatedChanged);
    QSignalSpy output(&process, &PM3Process::newOutput);
    QSignalSpy blocked(&process, &PM3Process::operationBlocked);
    fixture.launch(process);
    QCOMPARE(process.state(), QProcess::Running);
    QVERIFY(reportedState(state, true));
    QCOMPARE(fixture.starts(), 1);
    output.clear();

    const QStringList commands = {QStringLiteral("hw status"), QStringLiteral("data plot"),
        QStringLiteral("rem ") + QString::fromUtf8("\xe4\xb8\xad\xe6\x96\x87")};
    for (const QString &command : commands) {
        QVERIFY(process.write(command + QLatin1Char('\n')) > 0);
        QTRY_VERIFY_WITH_TIMEOUT(outputText(output).contains(received(command)), 2000);
    }
    QVERIFY(process.write(QStringLiteral("\r\n")) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(outputText(output).contains(QStringLiteral("RX:\n")), 2000);
    QCOMPARE(blocked.count(), 0);
    process.killPM3();
    QCOMPARE(process.state(), QProcess::NotRunning);
    QVERIFY(reportedState(state, false));
}

void ProcessBoundaryTest::blockedCommandsNeverReachBackend()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(), qPrintable(fixture.error));
    PM3Process process(nullptr);
    QSignalSpy output(&process, &PM3Process::newOutput);
    QSignalSpy blocked(&process, &PM3Process::operationBlocked);
    fixture.launch(process);
    QCOMPARE(process.state(), QProcess::Running);
    output.clear();

    const QStringList commands = {
        QStringLiteral("mqtt pub -t example -m example\n"),
        QStringLiteral("!whoami\n"), QStringLiteral("hf mf !whoami\n"),
        QStringLiteral("script run local\n"),
        QStringLiteral("hw status;script run local\n"),
        QStringLiteral("hw status\nmqtt pub -t example\n"),
        QStringLiteral("hw status\n\n")
    };
    for (const QString &command : commands)
        QCOMPARE(process.write(command), qint64(-1));
    QCOMPARE(blocked.count(), commands.size());

    // A later allowed command is a barrier in the real stdin stream; no sleep-
    // based absence assertion. Everything echoed before it must be this marker.
    const QString barrier = QStringLiteral("rem allowed-barrier");
    QVERIFY(process.write(barrier) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(outputText(output).contains(received(barrier)), 2000);
    QCOMPARE(outputText(output), received(barrier));
    process.killPM3();
}

void ProcessBoundaryTest::checksumMismatchNeverStarts()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(), qPrintable(fixture.error));
    PM3Process process(nullptr);
    QSignalSpy started(&process, &QProcess::started);
    QSignalSpy blocked(&process, &PM3Process::operationBlocked);
    QSignalSpy state(&process, &PM3Process::PM3StatedChanged);
    process.setWorkingDir(fixture.directory);
    process.connectPM3(fixture.executable, QStringList{QStringLiteral("COM3"), QStringLiteral("-f")}, QByteArray(64, '0'));
    QCOMPARE(process.state(), QProcess::NotRunning);
    QCOMPARE(started.count(), 0);
    QCOMPARE(blocked.count(), 1);
    QCOMPARE(fixture.starts(), 0);
    QVERIFY(reportedState(state, false));
    QVERIFY(!reportedState(state, true));
}

void ProcessBoundaryTest::invalidArgumentsNeverStart()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(), qPrintable(fixture.error));
    PM3Process process(nullptr);
    QSignalSpy started(&process, &QProcess::started);
    QSignalSpy blocked(&process, &PM3Process::operationBlocked);
    process.setWorkingDir(fixture.directory);
    process.connectPM3(fixture.executable, QStringList{QStringLiteral("tcp:localhost:18888"), QStringLiteral("-f")}, fixture.expectedHash);
    process.connectPM3(fixture.executable, QStringList{QStringLiteral("COM3"), QStringLiteral("-f"),
                       QStringLiteral("-c"), QStringLiteral("script run local")}, fixture.expectedHash);
    QCOMPARE(process.state(), QProcess::NotRunning);
    QCOMPARE(started.count(), 0);
    QCOMPARE(blocked.count(), 2);
    QCOMPARE(fixture.starts(), 0);
}

void ProcessBoundaryTest::invalidWorkspaceNeverStarts()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(), qPrintable(fixture.error));
    PM3Process process(nullptr);
    QSignalSpy started(&process, &QProcess::started);
    QSignalSpy blocked(&process, &PM3Process::operationBlocked);
    process.setWorkingDir(QStringLiteral("\\\\host\\share"));
    process.connectPM3(fixture.executable, QStringList{QStringLiteral("COM3"), QStringLiteral("-f")}, fixture.expectedHash);
    QCOMPARE(process.state(), QProcess::NotRunning);
    QCOMPARE(started.count(), 0);
    QVERIFY(blocked.count() >= 2);
    QCOMPARE(fixture.starts(), 0);
}

void ProcessBoundaryTest::reconnectRechecksChecksum()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(), qPrintable(fixture.error));
    PM3Process process(nullptr);
    fixture.launch(process);
    QCOMPARE(process.state(), QProcess::Running);
    process.killPM3();
    QCOMPARE(process.state(), QProcess::NotRunning);
    QCOMPARE(fixture.starts(), 1);

    // A trailing byte preserves the native executable format but changes its
    // digest, exercising revalidation rather than the script/format check.
    QFile file(fixture.executable);
    QVERIFY(file.open(QIODevice::Append));
    QCOMPARE(file.write("X", 1), qint64(1));
    file.close();
    QVERIFY(digest(fixture.executable) != fixture.expectedHash);

    QSignalSpy started(&process, &QProcess::started);
    QSignalSpy blocked(&process, &PM3Process::operationBlocked);
    QSignalSpy state(&process, &PM3Process::PM3StatedChanged);
    process.reconnectPM3();
    QCOMPARE(process.state(), QProcess::NotRunning);
    QCOMPARE(started.count(), 0);
    QCOMPARE(blocked.count(), 1);
    QCOMPARE(fixture.starts(), 1);
    QVERIFY(reportedState(state, false));
}

void ProcessBoundaryTest::reconnectUnchangedBackend()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(), qPrintable(fixture.error));
    PM3Process process(nullptr);
    fixture.launch(process);
    QCOMPARE(process.state(), QProcess::Running);
    process.killPM3();
    QSignalSpy state(&process, &PM3Process::PM3StatedChanged);
    process.reconnectPM3();
    QCOMPARE(process.state(), QProcess::Running);
    QCOMPARE(fixture.starts(), 2);
    QVERIFY(reportedState(state, true));
    process.killPM3();
}

void ProcessBoundaryTest::serialListenerCanBeReenabled()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(), qPrintable(fixture.error));
    PM3Process process(nullptr);
    fixture.launch(process);
    QCOMPARE(process.state(), QProcess::Running);
    QSignalSpy blocked(&process, &PM3Process::operationBlocked);
    // Do not run the enumeration timer: this is a fixture, not a real COM3.
    // Exercise both slots synchronously, then stop it before processing events.
    process.setSerialListener(QStringLiteral("COM3"), true);
    process.setSerialListener(false);
    process.setSerialListener(true);
    process.setSerialListener(false);
    QCOMPARE(blocked.count(), 0);
    process.killPM3();
}

void ProcessBoundaryTest::unexpectedExitReportsDisconnected()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(), qPrintable(fixture.error));
    PM3Process process(nullptr);
    fixture.launch(process);
    QCOMPARE(process.state(), QProcess::Running);
    QSignalSpy state(&process, &PM3Process::PM3StatedChanged);
    QVERIFY(process.write(QStringLiteral("rem fixture-exit\n")) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(reportedState(state, false), 2000);
    QCOMPARE(process.exitCode(), 23);
    QVERIFY(!reportedState(state, true));
}

void ProcessBoundaryTest::startupExitNeverReportsConnected()
{
    Fixture fixture;
    QVERIFY2(fixture.prepare(QStringLiteral("exit-on-start")), qPrintable(fixture.error));
    PM3Process process(nullptr);
    QSignalSpy state(&process, &PM3Process::PM3StatedChanged);
    fixture.launch(process);
    QCOMPARE(process.state(), QProcess::NotRunning);
    QVERIFY(reportedState(state, false));
    QVERIFY(!reportedState(state, true));
    QCOMPARE(fixture.starts(), 1);
}

void ProcessBoundaryTest::handshakeAcrossChunks_data()
{
    QTest::addColumn<QString>("mode");
    QTest::newRow("split-banner") << QStringLiteral("split-banner");
    QTest::newRow("split-version") << QStringLiteral("split-version");
}

void ProcessBoundaryTest::handshakeAcrossChunks()
{
    QFETCH(QString, mode);
    Fixture fixture;
    QVERIFY2(fixture.prepare(mode), qPrintable(fixture.error));
    PM3Process process(nullptr);
    QSignalSpy state(&process, &PM3Process::PM3StatedChanged);
    fixture.launch(process);
    QCOMPARE(process.state(), QProcess::Running);
    QVERIFY(reportedState(state, true));
    process.killPM3();
}

void ProcessBoundaryTest::writeWhileDisconnectedIsBlocked()
{
    PM3Process process(nullptr);
    QSignalSpy blocked(&process, &PM3Process::operationBlocked);
    QCOMPARE(process.write(QStringLiteral("hw version\n")), qint64(-1));
    QCOMPARE(blocked.count(), 1);
}

void ProcessBoundaryTest::utilRejectsBeforeEmitting()
{
    Util util;
    util.setRunningState(true);
    QSignalSpy written(&util, &Util::write);
    QSignalSpy blocked(&util, &Util::operationBlocked);
    util.execCMD(QStringLiteral("mqtt pub -t example"));
    QCOMPARE(util.execCMDWithOutput(QStringLiteral("script run local")), QString());
    QCOMPARE(blocked.count(), 2);
    QCOMPARE(written.count(), 0);
    util.execCMD(QStringLiteral("hw status"));
    QCOMPARE(written.count(), 1);
    QCOMPARE(written.at(0).at(0).toString(), QStringLiteral("hw status\n"));
}

QTEST_GUILESS_MAIN(ProcessBoundaryTest)
#include "tst_processboundary.moc"
