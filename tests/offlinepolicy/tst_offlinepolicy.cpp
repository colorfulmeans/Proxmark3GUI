#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "offlinepolicy.h"

class OfflinePolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void commands_data();
    void commands();
    void framing_data();
    void framing();
    void ports_data();
    void ports();
    void launchArguments();
    void executablePaths();
    void localConfigurationPaths();
    void templates();
    void shippedConfigurationTemplates();
    void lengthBoundary();
};

void OfflinePolicyTest::commands_data()
{
    QTest::addColumn<QString>("command");
    QTest::addColumn<bool>("allowed");

    const QStringList permitted = {
        QString(), QStringLiteral("   "), QStringLiteral("hw version"),
        QStringLiteral("HW VERSION"), QStringLiteral("  hf   14a   info -nsv  "),
        QStringLiteral("hf mf rdbl --blk 4 -a -k FFFFFFFFFFFF"),
        QStringLiteral("hf mf wrbl --blk 4 -a -k FFFFFFFFFFFF -d 00112233445566778899AABBCCDDEEFF --force"),
        QStringLiteral("hf mf rdbl 4 A FFFFFFFFFFFF"),
        QStringLiteral("hf mf nested 1 *"), QStringLiteral("hf mf chk *1 ?"),
        QStringLiteral("hf mf sim --1k -u 01020304 -n 3 -i"),
        QStringLiteral("hf 14a raw -ak -b 7 40"),
        QStringLiteral("hf 14a raw -ak E0 00 39 F7"),
        QStringLiteral("lf config --divisor 95 --bps 8 --dec 1 --avg 1 --trig -1 --skip 0"),
        QStringLiteral("lf em 410x reader"), QStringLiteral("lf em 410x clone --id 0102030405 --q5"),
        QStringLiteral("lf em 410xwrite 0102030405 1"),
        QStringLiteral("lf t55xx info"), QStringLiteral("hf search"),
        QStringLiteral("data plot"), QStringLiteral("trace list --buffer -t mf"),
        QStringLiteral("trace save -f local-trace.bin"),
        QStringLiteral("trace load -f \"local folder/trace.bin\""),
        QStringLiteral("trace load -f '/tmp/local trace.bin'"),
        QStringLiteral("trace save -f mqtt-script-python-tcp.bin"),
        QStringLiteral("hf list mf -s local-trace.bin"),
        QStringLiteral("prefs show"), QStringLiteral("rem local-note"),
        QStringLiteral("hf"), QStringLiteral("lf"), QStringLiteral("HW --help"),
        QStringLiteral("hf mf"), QStringLiteral("hf mf -h"), QStringLiteral("data")
    };
    int index = 0;
    for (const QString &command : permitted) {
        const QByteArray name = QByteArray("allowed-") + QByteArray::number(index++);
        QTest::newRow(name.constData()) << command << true;
    }
    const QStringList denied = {
        QStringLiteral("script run local"), QStringLiteral("SCRIPT RUN local"),
        QStringLiteral("s run local"), QStringLiteral("scr run local"),
        QStringLiteral("lua local.lua"), QStringLiteral("python local.py"),
        QStringLiteral("mqtt pub -t local -m data"), QStringLiteral("MQTT help"),
        QStringLiteral("hw connect tcp:127.0.0.1:18888"),
        QStringLiteral("hw connect COM3"), QStringLiteral("alias safe script run local"),
        QStringLiteral("hf mf !whoami"), QStringLiteral("hf !whoami"), QStringLiteral("!whoami"),
        QStringLiteral("hf mf rdbl; script run local"),
        QStringLiteral("hf mf rdbl && !whoami"), QStringLiteral("hw status | more"),
        QStringLiteral("hw version > result.txt"), QStringLiteral("hw version < input.txt"),
        QStringLiteral("hw version `whoami`"), QStringLiteral("hw version $(whoami)"),
        QStringLiteral("hw version %COMSPEC%"), QStringLiteral("hw version ^& whoami"),
        QStringLiteral("trace load -f \"local;script run local\""),
        QStringLiteral("hf mf rdbl\nscript run local"), QStringLiteral("hf mf rdbl\rscript run local"),
        QStringLiteral("hw version\n"), QStringLiteral("hf\tmf rdbl"),
        QStringLiteral("hf mf r --blk 4"), QStringLiteral("h ver"),
        QStringLiteral("hf script run local"), QStringLiteral("hf -c local"),
        QStringLiteral("hf mf --help script run local"),
        QStringLiteral("trace lsit"), QStringLiteral("hf mf unknown"),
        QStringLiteral("'hf' mf rdbl"), QStringLiteral("hf \"mf\" rdbl"),
        QStringLiteral("trace load -f \"unclosed"), QStringLiteral("trace load -f\"local.bin\""),
        QStringLiteral("trace load -f http://example.invalid/data.bin"),
        QStringLiteral("trace load -f HTTPS://example.invalid/data.bin"),
        QStringLiteral("trace load -f file:///tmp/local.bin"),
        QStringLiteral("trace load -f tcp:localhost:18888"),
        QStringLiteral("trace load -f \\\\server\\share\\file.bin"),
        QStringLiteral("trace load -f //server/share/file.bin"),
        QStringLiteral("trace load -f /\\server/share/file.bin"),
        QStringLiteral("trace load -f\\\\server\\share\\file.bin"),
        QStringLiteral("trace load --file=\\\\server\\share\\file.bin"),
        QStringLiteral("trace load -f C:relative.bin"),
        QStringLiteral("trace load -f C:/local.bin:alternate"),
        QStringLiteral("trace load -f \\\\?\\C:\\local.bin"),
        QStringLiteral("hw version") + QChar(0),
        QStringLiteral("hw version") + QChar(0x1b),
        QStringLiteral("hw") + QChar(0xa0) + QStringLiteral("version"),
        QStringLiteral("hw version") + QChar(0x2028) + QStringLiteral("script run local"),
        QStringLiteral("hw version") + QChar(0x202e)
    };
    index = 0;
    for (const QString &command : denied) {
        const QByteArray name = QByteArray("blocked-") + QByteArray::number(index++);
        QTest::newRow(name.constData()) << command << false;
    }
}

void OfflinePolicyTest::commands()
{
    QFETCH(QString, command);
    QFETCH(bool, allowed);
    QString error = QStringLiteral("stale");
    const bool result = OfflinePolicy::validateCommand(command, &error);
    QCOMPARE(result, allowed);
    if (allowed)
        QVERIFY2(error.isEmpty(), qPrintable(error));
    else
        QVERIFY(!error.isEmpty());
}

void OfflinePolicyTest::framing_data()
{
    QTest::addColumn<QString>("frame");
    QTest::addColumn<QString>("normalized");
    QTest::addColumn<bool>("allowed");
    QTest::newRow("plain") << QStringLiteral("hw version") << QStringLiteral("hw version") << true;
    QTest::newRow("lf") << QStringLiteral("hw version\n") << QStringLiteral("hw version") << true;
    QTest::newRow("cr") << QStringLiteral("hw version\r") << QStringLiteral("hw version") << true;
    QTest::newRow("crlf") << QStringLiteral("hw version\r\n") << QStringLiteral("hw version") << true;
    QTest::newRow("empty") << QString() << QString() << true;
    QTest::newRow("enter") << QStringLiteral("\n") << QString() << true;
    QTest::newRow("enter-crlf") << QStringLiteral("\r\n") << QString() << true;
    QTest::newRow("two-enters") << QStringLiteral("\n\n") << QString() << false;
    QTest::newRow("two-commands") << QStringLiteral("hw version\nhw status\n") << QString() << false;
    QTest::newRow("reversed-ending") << QStringLiteral("hw version\n\r") << QString() << false;
    QTest::newRow("second-frame") << QStringLiteral("hw version\n\n") << QString() << false;
}

void OfflinePolicyTest::framing()
{
    QFETCH(QString, frame);
    QFETCH(QString, normalized);
    QFETCH(bool, allowed);
    QString output = QStringLiteral("stale");
    QString error;
    QCOMPARE(OfflinePolicy::normalizeCommandFrame(frame, &output, &error), allowed);
    QCOMPARE(output, normalized);
    QCOMPARE(error.isEmpty(), allowed);
}

void OfflinePolicyTest::ports_data()
{
    QTest::addColumn<QString>("port");
    QTest::addColumn<QString>("normalized");
    QTest::addColumn<bool>("allowed");
    QTest::newRow("com") << QStringLiteral("COM3") << QStringLiteral("COM3") << true;
    QTest::newRow("com-case") << QStringLiteral("com123") << QStringLiteral("COM123") << true;
    QTest::newRow("linux-acm") << QStringLiteral("/dev/ttyACM0") << QStringLiteral("/dev/ttyACM0") << true;
    QTest::newRow("linux-usb") << QStringLiteral("/dev/ttyUSB0") << QStringLiteral("/dev/ttyUSB0") << true;
    QTest::newRow("mac") << QStringLiteral("/dev/cu.usbmodem123") << QStringLiteral("/dev/cu.usbmodem123") << true;
    const QStringList bad = {
        QString(), QStringLiteral("COM0"), QStringLiteral("COM01"), QStringLiteral("COM3:"),
        QStringLiteral(" COM3"), QStringLiteral("COM3 "), QStringLiteral("COM3\n"),
        QStringLiteral("COM3\r"), QStringLiteral("COM3 -c script"),
        QStringLiteral("COM3;script run local"), QStringLiteral("tcp:host:18888"),
        QStringLiteral("TCP:host:18888"), QStringLiteral("udp:host:18888"),
        QStringLiteral("socket:/tmp/pm3"), QStringLiteral("bt:11:22:33:44:55:66"),
        QStringLiteral("\\\\.\\COM3"), QStringLiteral("\\\\host\\COM3"),
        QStringLiteral("/dev/tty/../../tmp/file"), QStringLiteral("/tmp/ttyACM0"),
        QStringLiteral("/dev/ttyACM0\n"), QStringLiteral("/dev/cu.foo/bar"),
        QStringLiteral("/dev/serial/by-id/unresolved-link")
    };
    int index = 0;
    for (const QString &port : bad) {
        const QByteArray name = QByteArray("blocked-") + QByteArray::number(index++);
        QTest::newRow(name.constData()) << port << QString() << false;
    }
}

void OfflinePolicyTest::ports()
{
    QFETCH(QString, port);
    QFETCH(QString, normalized);
    QFETCH(bool, allowed);
    QString output = QStringLiteral("stale");
    QString error;
    QCOMPARE(OfflinePolicy::validateSerialPort(port, &output, &error), allowed);
    QCOMPARE(output, normalized);
    QCOMPARE(error.isEmpty(), allowed);
}

void OfflinePolicyTest::launchArguments()
{
    QStringList args;
    QString error;
    QVERIFY(OfflinePolicy::buildLaunchArguments(QStringLiteral("com3"), &args, &error));
    QCOMPARE(args, (QStringList{QStringLiteral("COM3"), QStringLiteral("-f")}));
    QVERIFY(OfflinePolicy::validateLaunchArguments(args, &error));
    const QStringList flags = {QStringLiteral("-c"), QStringLiteral("--command"),
        QStringLiteral("-s"), QStringLiteral("--script-file"), QStringLiteral("-l"),
        QStringLiteral("--lua"), QStringLiteral("-y"), QStringLiteral("--py"),
        QStringLiteral("--flash"), QStringLiteral("-w"), QStringLiteral("--wait"),
        QStringLiteral("--"), QStringLiteral("-i"), QStringLiteral("--incognito")};
    for (const QString &flag : flags) {
        QStringList modified = args;
        modified << flag << QStringLiteral("local");
        QVERIFY2(!OfflinePolicy::validateLaunchArguments(modified, &error), qPrintable(flag));
    }
    QVERIFY(!OfflinePolicy::validateLaunchArguments(QStringList(), &error));
    QVERIFY(!OfflinePolicy::validateLaunchArguments(QStringList{QStringLiteral("COM3")}, &error));
    QVERIFY(!OfflinePolicy::validateLaunchArguments(QStringList{QStringLiteral("COM3"), QStringLiteral("-F")}, &error));
    QVERIFY(!OfflinePolicy::validateLaunchArguments(QStringList{QStringLiteral("tcp:localhost:18888"), QStringLiteral("-f")}, &error));
    QVERIFY(!OfflinePolicy::validateLaunchArguments(QStringList{QStringLiteral("com3"), QStringLiteral("-f")}, &error));
    QVERIFY(!OfflinePolicy::buildLaunchArguments(QStringLiteral("COM3 -c local"), &args, &error));
    QVERIFY(args.isEmpty());
}

static bool writeFixture(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return false;
    file.close();
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void OfflinePolicyTest::executablePaths()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QString realPath;
    QString error;
    const QString ownExecutable = QCoreApplication::applicationFilePath();
    QVERIFY2(OfflinePolicy::validateExecutablePath(ownExecutable, &realPath, &error), qPrintable(error));
    QCOMPARE(realPath, QFileInfo(ownExecutable).canonicalFilePath());

    const QString script = temporary.filePath(QStringLiteral("pretend-native.exe"));
    QVERIFY(writeFixture(script, QByteArray("#!/bin/sh\nexit 0\n") + QByteArray(128, '#')));
    QVERIFY(!OfflinePolicy::validateExecutablePath(script, &realPath, &error));
    QVERIFY(realPath.isEmpty());
    const QString batch = temporary.filePath(QStringLiteral("backend.CmD"));
    QVERIFY(writeFixture(batch, QByteArray("@echo off\n") + QByteArray(128, ' ')));
    QVERIFY(!OfflinePolicy::validateExecutablePath(batch, &realPath, &error));
    const QString launcher = temporary.filePath(QStringLiteral("powershell.exe"));
    QVERIFY(QFile::copy(ownExecutable, launcher));
    QVERIFY(!OfflinePolicy::validateExecutablePath(launcher, &realPath, &error));

    const QStringList invalid = {
        QStringLiteral("proxmark3.exe"), QStringLiteral("./proxmark3"),
        QStringLiteral("https://example.invalid/proxmark3.exe"),
        QStringLiteral("file:///tmp/proxmark3"), QStringLiteral("\\\\server\\share\\proxmark3.exe"),
        QStringLiteral("//server/share/proxmark3.exe"),
        QStringLiteral("\\\\?\\C:\\proxmark3.exe"),
        QStringLiteral("C:proxmark3.exe"), temporary.path(),
        temporary.filePath(QStringLiteral("missing")), ownExecutable + QChar(0)
    };
    for (const QString &path : invalid) {
        realPath = QStringLiteral("stale");
        QVERIFY2(!OfflinePolicy::validateExecutablePath(path, &realPath, &error), qPrintable(path));
        QVERIFY(realPath.isEmpty());
        QVERIFY(!error.isEmpty());
    }
#ifndef Q_OS_WIN
    // A benign symlink is resolved, and the caller is given the canonical path.
    const QString goodLink = temporary.filePath(QStringLiteral("pm3-link"));
    QVERIFY(QFile::link(ownExecutable, goodLink));
    QVERIFY2(OfflinePolicy::validateExecutablePath(goodLink, &realPath, &error), qPrintable(error));
    QCOMPARE(realPath, QFileInfo(ownExecutable).canonicalFilePath());
    const QString badLink = temporary.filePath(QStringLiteral("pm3-script-link"));
    QVERIFY(QFile::link(script, badLink));
    QVERIFY(!OfflinePolicy::validateExecutablePath(badLink, &realPath, &error));
    const QString noExecute = temporary.filePath(QStringLiteral("pm3-no-execute"));
    QVERIFY(QFile::copy(ownExecutable, noExecute));
    QVERIFY(QFile::setPermissions(noExecute, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY(!OfflinePolicy::validateExecutablePath(noExecute, &realPath, &error));
#endif
}

void OfflinePolicyTest::localConfigurationPaths()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString config = temporary.filePath(QStringLiteral("local configuration.json"));
    QVERIFY(writeFixture(config, "{}"));
    QString canonical;
    QString error;
    QVERIFY2(OfflinePolicy::validateLocalFilePath(config, &canonical, &error), qPrintable(error));
    QCOMPARE(canonical, QFileInfo(config).canonicalFilePath());
    QVERIFY(!OfflinePolicy::validateLocalFilePath(QStringLiteral("https://example.invalid/config.json"), &canonical, &error));
    QVERIFY(!OfflinePolicy::validateLocalFilePath(QStringLiteral("\\\\host\\config.json"), &canonical, &error));
    QVERIFY(!OfflinePolicy::validateLocalFilePath(QStringLiteral("config.json"), &canonical, &error));
    QVERIFY(!OfflinePolicy::validateLocalFilePath(temporary.path(), &canonical, &error));
    QVERIFY(canonical.isEmpty());
}

void OfflinePolicyTest::templates()
{
    QString error;
    QVERIFY(OfflinePolicy::validateCommandTemplate(QStringLiteral("hf mf rdbl --blk <block> -<key type> -k <key>"), &error));
    QVERIFY(OfflinePolicy::validateCommandTemplate(QStringLiteral("trace save -f <filename>"), &error));
    QVERIFY(!OfflinePolicy::validateCommandTemplate(QStringLiteral("<command>"), &error));
    QVERIFY(!OfflinePolicy::validateCommandTemplate(QStringLiteral("hf mf rdbl;script run local"), &error));
    QVERIFY(!OfflinePolicy::validateCommandTemplate(QStringLiteral("hf mf rdbl <unknown>"), &error));
    QVERIFY(!OfflinePolicy::validateCommandTemplate(QStringLiteral("trace load -f <filename>\n!whoami"), &error));
    // Template approval is deliberately not reused for substituted commands.
    const QString substituted = QStringLiteral("trace save -f <filename>")
            .replace(QStringLiteral("<filename>"), QStringLiteral("local.bin;script run local"));
    QVERIFY(!OfflinePolicy::validateCommand(substituted, &error));
}

static void collectTemplates(const QJsonObject &object, const QString &prefix,
                             QList<QPair<QString, QString> > *commands)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString path = prefix + QLatin1Char('/') + it.key();
        if (it.value().isObject()) {
            collectTemplates(it.value().toObject(), path, commands);
        } else if (it.value().isString() && (it.key().endsWith(QStringLiteral("cmd"))
                                           || it.key() == QStringLiteral("read"))) {
            commands->append(qMakePair(path, it.value().toString()));
        } else if (it.key() == QStringLiteral("sequence") && it.value().isArray()) {
            for (const QJsonValue &value : it.value().toArray())
                commands->append(qMakePair(path, object.value(QStringLiteral("cmd")).toString() + value.toString()));
        }
    }
}

void OfflinePolicyTest::shippedConfigurationTemplates()
{
    const QString directory = QFINDTESTDATA("../../config");
    QVERIFY2(!directory.isEmpty(), "The shipped config directory is required for this regression test.");
    const QStringList profiles = QDir(directory).entryList(QStringList{QStringLiteral("*.json")}, QDir::Files);
    QVERIFY(profiles.size() >= 4);
    int checked = 0;
    for (const QString &profile : profiles) {
        QFile file(QDir(directory).filePath(profile));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QList<QPair<QString, QString> > commands;
        collectTemplates(document.object(), profile, &commands);
        for (const auto &entry : commands) {
            QString error;
            const bool accepted = OfflinePolicy::validateCommandTemplate(entry.second, &error);
            const QString context = entry.first + QStringLiteral(": ") + error;
            QVERIFY2(accepted, qPrintable(context));
            ++checked;
        }
    }
    QVERIFY(checked >= 140);
}

void OfflinePolicyTest::lengthBoundary()
{
    QString error;
    const QString longest = QStringLiteral("rem ") + QString(250, QLatin1Char('x'));
    QCOMPARE(longest.toUtf8().size(), 254);
    QVERIFY(OfflinePolicy::validateCommand(longest, &error));
    QVERIFY(!OfflinePolicy::validateCommand(longest + QLatin1Char('x'), &error));
    const QString multibyte = QStringLiteral("rem ") + QString(90, QChar(0x4e2d));
    QVERIFY(multibyte.size() < 254);
    QVERIFY(multibyte.toUtf8().size() > 254);
    QVERIFY(!OfflinePolicy::validateCommand(multibyte, &error));
}

QTEST_GUILESS_MAIN(OfflinePolicyTest)
#include "tst_offlinepolicy.moc"
