#ifndef PM3PROCESS_H
#define PM3PROCESS_H

#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QScopedPointer>
#include <QTextCodec>
#include <QtSerialPort/QSerialPortInfo>
#include "util.h"

class PM3Process : public QProcess
{
    Q_OBJECT
public:
    explicit PM3Process(QThread* thread, QObject* parent = nullptr);
    ~PM3Process() override;
    bool waitForReadyRead(int msecs = 2000);
    void testThread();

public slots:
    void connectPM3(const QString& path, const QStringList args, const QByteArray& expectedHash);
    void setSerialListener(const QString& name, bool state);
    void setSerialListener(bool state);
    qint64 write(QString data);
    void reconnectPM3();
    void setWorkingDir(const QString& dir);
    void killPM3();
private slots:
    void onTimeout();
    void onReadyRead();
private:
    bool isRequiringOutput = false;
    QString requiredOutput;
    void setRequiringOutput(bool st);
    QTimer* serialListener;
    QString currPath;
    QString currPort;
    QStringList currArgs;
    QByteArray currHash;
    QScopedPointer<QTextDecoder> outputDecoder;

signals:
    void PM3StatedChanged(bool st, const QString& info = "");
    void newOutput(const QString& output);
    void changeClientType(Util::ClientType);
    void HWConnectFailed();
    void operationBlocked(const QString& error);
};
#endif
