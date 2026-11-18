#ifndef LOCALDICTIONARY_H
#define LOCALDICTIONARY_H

#include <QList>
#include <QString>
#include <QStringList>

// Local text dictionaries only. This class does not run a client or contact a
// service. Paths are checked before opening them, including existing symlinks.
class LocalDictionary
{
public:
    struct ParseResult
    {
        QStringList keys;
        QList<int> invalidLines;
        int duplicateCount = 0;

        bool isValid() const { return invalidLines.isEmpty(); }
        QString errorString() const;
    };

    struct DictionaryInfo
    {
        QString name;
        QString absolutePath;
        int keyCount = 0;
        int duplicateCount = 0;
        QString error;

        bool isValid() const { return error.isEmpty(); }
    };

    // directoryPath is the managed dictionary directory itself, not its parent.
    explicit LocalDictionary(const QString& directoryPath);

    QString directoryPath() const;
    bool ensureDirectory(QString* error = nullptr) const;
    QList<DictionaryInfo> dictionaries(QString* error = nullptr) const;

    // name is a filename stem. Imports never overwrite an existing dictionary.
    // A single invalid line rejects the entire import; no partial file is saved.
    bool importFile(const QString& sourcePath, const QString& name,
                    DictionaryInfo* imported = nullptr, QString* error = nullptr) const;

    static ParseResult parse(const QString& text);
    static bool readFile(const QString& path, ParseResult* result, QString* error = nullptr);
    static bool writeFile(const QString& path, const QStringList& keys, QString* error = nullptr);
    static bool exportFile(const QString& sourcePath, const QString& destinationPath,
                           QString* error = nullptr);

    // Accepts local absolute or relative filesystem paths and returns an
    // absolute path. URLs, UNC/device paths, mapped network drives on Windows,
    // and existing symlink components are rejected.
    static bool validateLocalPath(const QString& path, QString* absolutePath = nullptr,
                                  QString* error = nullptr);
    static bool validateName(const QString& name, QString* error = nullptr);
    static qint64 maximumFileSize();

private:
    QString m_directoryPath;
};

#endif // LOCALDICTIONARY_H
