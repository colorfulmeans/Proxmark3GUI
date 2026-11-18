#include "apppaths.h"
#include "localdictionary.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

QString AppPaths::dataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString AppPaths::workspaceDirectory()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("workspace"));
}

QString AppPaths::settingsFile()
{
    return QDir(dataDirectory()).filePath(QStringLiteral("GUIsettings.ini"));
}

bool AppPaths::initialize(QString* error)
{
    const QString root = dataDirectory();
    if(root.isEmpty())
    {
        if(error) *error = QStringLiteral("No local application data directory is available.");
        return false;
    }
    const QStringList directories = {root, workspaceDirectory(),
        QDir(root).filePath(QStringLiteral("dictionaries")),
        QDir(root).filePath(QStringLiteral("logs"))};
    for(const QString& path : directories)
    {
        if(!LocalDictionary::validateLocalPath(path, nullptr, error))
            return false;
        if(!QDir().mkpath(path))
        {
            if(error) *error = QStringLiteral("Cannot create local data directory: %1").arg(path);
            return false;
        }
#ifndef Q_OS_WIN
        if(!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        {
            if(error) *error = QStringLiteral("Cannot restrict the local data directory to its owner: %1").arg(path);
            return false;
        }
#endif
    }
    return true;
}
