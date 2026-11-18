#include "pm3process.h"
#include "offlinepolicy.h"
#include "localdictionary.h"

#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QElapsedTimer>

PM3Process::PM3Process(QThread* thread, QObject* parent): QProcess(parent), serialListener(new QTimer(this))
{
    setProcessChannelMode(QProcess::MergedChannels);
    serialListener->setInterval(1000);
    serialListener->setTimerType(Qt::VeryCoarseTimer);
    connect(serialListener, &QTimer::timeout, this, &PM3Process::onTimeout);
    connect(this, &QProcess::readyRead, this, &PM3Process::onReadyRead);
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
        onReadyRead();
        serialListener->stop();
        setRequiringOutput(false);
        emit PM3StatedChanged(false);
    });
    qRegisterMetaType<QProcess::ProcessError>("QProcess::ProcessError");
    if(thread) moveToThread(thread);
}

PM3Process::~PM3Process()
{
    if(state() != QProcess::NotRunning)
    {
        kill();
        waitForFinished(2000);
    }
}

void PM3Process::connectPM3(const QString& path, const QStringList args, const QByteArray& expectedHash)
{
    if(state() != QProcess::NotRunning)
    {
        emit operationBlocked(tr("The client is already running."));
        return;
    }
    QString canonicalPath, error;
    if(!OfflinePolicy::validateExecutablePath(path, &canonicalPath, &error)
       || !OfflinePolicy::validateLaunchArguments(args, &error))
    {
        emit operationBlocked(error);
        emit PM3StatedChanged(false);
        return;
    }
    QFile client(canonicalPath);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(expectedHash.size() != 64 || !client.open(QIODevice::ReadOnly) || !hash.addData(&client)
       || hash.result().toHex() != expectedHash)
    {
        emit operationBlocked(tr("The client checksum does not match the explicitly selected file. Choose the client again."));
        emit PM3StatedChanged(false);
        return;
    }
    client.close();
    if(!LocalDictionary::validateLocalPath(workingDirectory(), nullptr, &error)
       || !QDir(workingDirectory()).exists())
    {
        emit operationBlocked(tr("The local client workspace is unavailable: %1").arg(error));
        emit PM3StatedChanged(false);
        return;
    }
    currPath = canonicalPath;
    currArgs = args;
    currHash = expectedHash;
    outputDecoder.reset(QTextCodec::codecForName("UTF-8")->makeDecoder());
    setRequiringOutput(true);
    start(canonicalPath, args, QProcess::Unbuffered | QProcess::ReadWrite);
    if(!waitForStarted(10000))
    {
        setRequiringOutput(false);
        emit PM3StatedChanged(false);
        return;
    }
    const QRegularExpression osPattern("(?:os:[ \\t]+|OS\\.+[ \\t]+)([^\\r\\n]+)\\r?\\n");
    QElapsedTimer handshake;
    handshake.start();
    bool requestedVersion = false;
    QString result;
    while(state() != QProcess::NotRunning && handshake.elapsed() < 10000)
    {
        result = requiredOutput;
        if(osPattern.match(result).hasMatch()) break;
        // A pipe can split even the three-byte [=] banner. Accumulate until a
        // complete marker/line exists instead of classifying the first chunk.
        if(!requestedVersion && result.contains("[=]"))
        {
            requestedVersion = true;
            write("hw version\n");
        }
        const qint64 remaining = 10000 - handshake.elapsed();
        if(remaining <= 0) break;
        waitForReadyRead(int(qMin<qint64>(200, remaining)));
    }
    result = requiredOutput;
    const Util::ClientType type = result.contains("[=]") ? Util::CLIENTTYPE_ICEMAN : Util::CLIENTTYPE_OFFICIAL;
    const QRegularExpressionMatch match = osPattern.match(result);
    setRequiringOutput(false);
    if(match.hasMatch() && state() != QProcess::NotRunning)
    {
        emit changeClientType(type);
        const QString info = match.captured(1).trimmed();
        emit PM3StatedChanged(true, info);
    }
    else
    {
        emit HWConnectFailed();
        killPM3();
    }
}

void PM3Process::reconnectPM3()
{
    // Reapply path, argument and digest checks on every launch.
    connectPM3(currPath, currArgs, currHash);
}

void PM3Process::setRequiringOutput(bool st)
{
    isRequiringOutput = st;
    if(st) requiredOutput.clear();
}

bool PM3Process::waitForReadyRead(int msecs)
{
    return QProcess::waitForReadyRead(msecs);
}

void PM3Process::setSerialListener(const QString& name, bool enabled)
{
    if(enabled && state() != QProcess::NotRunning)
    {
        QString error, normalizedPort;
        if(!OfflinePolicy::validateSerialPort(name, &normalizedPort, &error))
        {
            emit operationBlocked(error);
            return;
        }
        currPort = normalizedPort;
        serialListener->start();
    }
    else
        serialListener->stop();
}

void PM3Process::setSerialListener(bool enabled)
{
    setSerialListener(currPort, enabled);
}

void PM3Process::onTimeout()
{
    bool present = false;
    for(const QSerialPortInfo& info : QSerialPortInfo::availablePorts())
        if(info.portName() == currPort || info.systemLocation() == currPort)
            present = true;
    if(!present) killPM3();
}

void PM3Process::testThread()
{
    qDebug() << "PM3:" << QThread::currentThread();
}

qint64 PM3Process::write(QString frame)
{
    QString command, error;
    if(!OfflinePolicy::normalizeCommandFrame(frame, &command, &error))
    {
        emit operationBlocked(error);
        return -1;
    }
    if(state() != QProcess::Running)
    {
        emit operationBlocked(tr("The local client is not running."));
        return -1;
    }
    return QProcess::write((command + "\n").toUtf8());
}

void PM3Process::onReadyRead()
{
    const QByteArray bytes = readAll();
    const QString out = outputDecoder ? outputDecoder->toUnicode(bytes) : QString::fromUtf8(bytes);
    if(isRequiringOutput) requiredOutput.append(out);
    if(!out.isEmpty()) emit newOutput(out);
}

void PM3Process::setWorkingDir(const QString& dir)
{
    QString path, error;
    if(!LocalDictionary::validateLocalPath(dir, &path, &error) || !QDir(path).exists())
    {
        setWorkingDirectory(QString());
        emit operationBlocked(tr("Invalid local workspace: %1").arg(error));
        return;
    }
    setWorkingDirectory(path);
}

void PM3Process::killPM3()
{
    serialListener->stop();
    if(state() != QProcess::NotRunning)
    {
        kill();
        waitForFinished(3000);
    }
    setRequiringOutput(false);
    emit PM3StatedChanged(false);
}
