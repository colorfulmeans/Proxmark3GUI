#include "localdictionary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextCodec>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace
{
bool fail(QString* error, const QString& message)
{
    if(error)
        *error = message;
    return false;
}

void clearError(QString* error)
{
    if(error)
        error->clear();
}

bool isKey(const QString& key)
{
    if(key.size() != 12)
        return false;
    for(const QChar character : key)
    {
        const ushort value = character.unicode();
        if(!((value >= '0' && value <= '9') ||
             (value >= 'a' && value <= 'f') ||
             (value >= 'A' && value <= 'F')))
            return false;
    }
    return true;
}

bool hasForbiddenPathSyntax(const QString& path)
{
    QString portable = path;
    portable.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if(portable.startsWith(QStringLiteral("//")) ||
       portable.startsWith(QStringLiteral("/") + QStringLiteral("??") + QLatin1Char('/')))
        return true;

    static const QRegularExpression scheme(QStringLiteral("\\A[A-Za-z][A-Za-z0-9+.-]*:"));
    if(scheme.match(portable).hasMatch())
    {
#ifdef Q_OS_WIN
        static const QRegularExpression drive(QStringLiteral("\\A[A-Za-z]:/"));
        return portable.contains(QStringLiteral("://")) || !drive.match(portable).hasMatch();
#else
        return true;
#endif
    }
    return false;
}
}

LocalDictionary::LocalDictionary(const QString& directoryPath)
    : m_directoryPath(directoryPath)
{
}

QString LocalDictionary::ParseResult::errorString() const
{
    if(isValid())
        return QString();

    QStringList displayedLines;
    const int displayLimit = 12;
    for(int i = 0; i < invalidLines.size() && i < displayLimit; ++i)
        displayedLines.append(QString::number(invalidLines.at(i)));

    QString message = QObject::tr("Invalid dictionary line(s): %1. Each key must contain exactly 12 hexadecimal characters.")
                      .arg(displayedLines.join(QStringLiteral(", ")));
    if(invalidLines.size() > displayLimit)
        message += QObject::tr(" There are %1 invalid lines in total.").arg(invalidLines.size());
    message += QObject::tr(" No dictionary was saved.");
    return message;
}

LocalDictionary::ParseResult LocalDictionary::parse(const QString& text)
{
    ParseResult result;
    QSet<QString> seen;
    QString input = text;
    QTextStream stream(&input, QIODevice::ReadOnly);
    const QStringList markers = {QStringLiteral("#"), QStringLiteral(";"), QStringLiteral("//")};
    int lineNumber = 0;
    while(!stream.atEnd())
    {
        QString line = stream.readLine();
        ++lineNumber;
        if(lineNumber == 1 && line.startsWith(QChar(0xfeff)))
            line.remove(0, 1);

        // Full-line and trailing comments use the same three markers.
        int comment = line.size();
        for(const QString& marker : markers)
        {
            const int position = line.indexOf(marker);
            if(position >= 0 && position < comment)
                comment = position;
        }
        line = line.left(comment).trimmed();
        if(line.isEmpty())
            continue;
        if(!isKey(line))
        {
            result.invalidLines.append(lineNumber);
            continue;
        }

        const QString key = line.toUpper();
        if(seen.contains(key))
            ++result.duplicateCount;
        else
        {
            seen.insert(key);
            result.keys.append(key);
        }
    }
    return result;
}

bool LocalDictionary::validateLocalPath(const QString& path, QString* absolutePath, QString* error)
{
    clearError(error);
    if(absolutePath)
        absolutePath->clear();
    if(path.isEmpty() || path != path.trimmed())
        return fail(error, QObject::tr("Enter a local path without leading or trailing whitespace."));
    for(const QChar character : path)
    {
        if(character.isNull() || character.category() == QChar::Other_Control)
            return fail(error, QObject::tr("The path contains a control character."));
    }
    if(hasForbiddenPathSyntax(path))
        return fail(error, QObject::tr("URLs, network shares and device paths are not supported. Choose a local file or folder."));

#ifndef Q_OS_WIN
    // A Windows path must not accidentally become a relative POSIX filename.
    if(path.contains(QLatin1Char('\\')))
        return fail(error, QObject::tr("Use the local filesystem's path separators."));
#endif

    // Resolve relative paths without opening the target. Check the working
    // directory as well: a relative path must not hide a UNC location.
    if(!QDir::isAbsolutePath(path) && hasForbiddenPathSyntax(QDir::currentPath()))
        return fail(error, QObject::tr("The current directory is not a supported local folder."));
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if(hasForbiddenPathSyntax(absolute))
        return fail(error, QObject::tr("The resolved path is not a supported local path."));

    QString root;
#ifdef Q_OS_WIN
    if(absolute.size() < 3 || absolute.at(1) != QLatin1Char(':') || absolute.at(2) != QLatin1Char('/'))
        return fail(error, QObject::tr("Choose a path on a local drive."));
    root = absolute.left(3);
    const QString nativeRoot = QDir::toNativeSeparators(root);
    if(GetDriveTypeW(reinterpret_cast<LPCWSTR>(nativeRoot.utf16())) == DRIVE_REMOTE)
        return fail(error, QObject::tr("Mapped network drives are not supported."));
#else
    root = QStringLiteral("/");
#endif

    // Inspect components from the root down, so a symlink is rejected before
    // accessing anything through its target (which might be a network mount).
    const QStringList components = absolute.mid(root.size()).split(QLatin1Char('/'));
    QString current = root;
    for(const QString& component : components)
    {
        if(component.isEmpty())
            continue;
        current = QDir(current).filePath(component);
        if(QFileInfo(current).isSymLink())
            return fail(error, QObject::tr("Symbolic links are not supported for local dictionaries. Choose the actual local path."));
    }

    if(absolutePath)
        *absolutePath = absolute;
    return true;
}

bool LocalDictionary::validateName(const QString& name, QString* error)
{
    clearError(error);
    if(name.isEmpty() || name != name.trimmed() || name.size() > 100 ||
       name == QStringLiteral(".") || name == QStringLiteral("..") ||
       name.endsWith(QLatin1Char('.')))
        return fail(error, QObject::tr("Use a dictionary name of 1 to 100 characters without surrounding whitespace or a trailing dot."));

    const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    for(const QChar character : name)
    {
        if(character.isNull() || character.category() == QChar::Other_Control || forbidden.contains(character))
            return fail(error, QObject::tr("The dictionary name contains a reserved filename character."));
    }
    static const QRegularExpression reserved(
        QStringLiteral("\\A(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|\\z)"),
        QRegularExpression::CaseInsensitiveOption);
    if(reserved.match(name).hasMatch())
        return fail(error, QObject::tr("This dictionary name is reserved by Windows."));
    return true;
}

qint64 LocalDictionary::maximumFileSize()
{
    return 32 * 1024 * 1024;
}

bool LocalDictionary::readFile(const QString& path, ParseResult* result, QString* error)
{
    clearError(error);
    if(result)
        *result = ParseResult();
    QString absolute;
    if(!validateLocalPath(path, &absolute, error))
        return false;
    const QFileInfo info(absolute);
    if(!info.isFile())
        return fail(error, QObject::tr("The dictionary does not exist or is not a regular file."));
    if(info.size() > maximumFileSize())
        return fail(error, QObject::tr("The dictionary is too large. The maximum supported size is 32 MiB."));

    QFile file(absolute);
    if(!file.open(QIODevice::ReadOnly))
        return fail(error, QObject::tr("Cannot read the dictionary: %1").arg(file.errorString()));
    const QByteArray bytes = file.read(maximumFileSize() + 1);
    if(file.error() != QFile::NoError)
        return fail(error, QObject::tr("Cannot read the dictionary: %1").arg(file.errorString()));
    if(bytes.size() > maximumFileSize())
        return fail(error, QObject::tr("The dictionary is too large. The maximum supported size is 32 MiB."));

    QTextCodec::ConverterState state;
    const QString text = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    if(state.invalidChars != 0 || state.remainingChars != 0)
        return fail(error, QObject::tr("The dictionary is not valid UTF-8 text. Save it as UTF-8 and try again."));

    const ParseResult parsed = parse(text);
    if(result)
        *result = parsed;
    if(!parsed.isValid())
        return fail(error, parsed.errorString());
    if(parsed.keys.isEmpty())
        return fail(error, QObject::tr("The dictionary contains no keys. No dictionary was saved."));
    return true;
}

bool LocalDictionary::writeFile(const QString& path, const QStringList& keys, QString* error)
{
    clearError(error);
    QString absolute;
    if(!validateLocalPath(path, &absolute, error))
        return false;
    if(keys.isEmpty())
        return fail(error, QObject::tr("The dictionary contains no keys. No dictionary was saved."));

    QStringList normalized;
    QSet<QString> seen;
    for(int index = 0; index < keys.size(); ++index)
    {
        if(!isKey(keys.at(index)))
            return fail(error, QObject::tr("Key %1 is invalid. No dictionary was saved.").arg(index + 1));
        const QString key = keys.at(index).toUpper();
        if(!seen.contains(key))
        {
            normalized.append(key);
            seen.insert(key);
        }
    }
    if(qint64(normalized.size()) * 13 > maximumFileSize())
        return fail(error, QObject::tr("The dictionary is too large. The maximum supported size is 32 MiB."));
    if(QFileInfo(absolute).exists() && !QFileInfo(absolute).isFile())
        return fail(error, QObject::tr("The destination is not a regular file."));

    QSaveFile file(absolute);
    file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly))
        return fail(error, QObject::tr("Cannot save the dictionary: %1").arg(file.errorString()));
    const QByteArray output = (normalized.join(QLatin1Char('\n')) + QLatin1Char('\n')).toLatin1();
    if(file.write(output) != output.size())
    {
        const QString detail = file.errorString();
        file.cancelWriting();
        return fail(error, QObject::tr("Cannot save the dictionary: %1").arg(detail));
    }
    if(!file.commit())
        return fail(error, QObject::tr("Cannot finish saving the dictionary: %1").arg(file.errorString()));
    return true;
}

QString LocalDictionary::directoryPath() const
{
    QString absolute;
    validateLocalPath(m_directoryPath, &absolute);
    return absolute;
}

bool LocalDictionary::ensureDirectory(QString* error) const
{
    QString absolute;
    if(!validateLocalPath(m_directoryPath, &absolute, error))
        return false;
    const QFileInfo existing(absolute);
    if(existing.exists() && !existing.isDir())
        return fail(error, QObject::tr("The dictionary folder path points to a file."));
    if(!QDir().mkpath(absolute))
        return fail(error, QObject::tr("Cannot create the local dictionary folder."));
    if(!QFileInfo(absolute).isReadable())
        return fail(error, QObject::tr("Cannot read the local dictionary folder."));
    return true;
}

QList<LocalDictionary::DictionaryInfo> LocalDictionary::dictionaries(QString* error) const
{
    QList<DictionaryInfo> entries;
    if(!ensureDirectory(error))
        return entries;

    const QDir directory(directoryPath());
    const QFileInfoList files = directory.entryInfoList(QStringList() << QStringLiteral("*.dic"),
                                                       QDir::Files | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot,
                                                       QDir::Name | QDir::IgnoreCase);
    for(const QFileInfo& file : files)
    {
        DictionaryInfo entry;
        entry.name = file.completeBaseName();
        entry.absolutePath = file.absoluteFilePath();
        ParseResult parsed;
        if(readFile(entry.absolutePath, &parsed, &entry.error))
        {
            entry.keyCount = parsed.keys.size();
            entry.duplicateCount = parsed.duplicateCount;
        }
        entries.append(entry);
    }
    return entries;
}

bool LocalDictionary::importFile(const QString& sourcePath, const QString& name,
                                 DictionaryInfo* imported, QString* error) const
{
    clearError(error);
    if(imported)
        *imported = DictionaryInfo();
    if(!validateName(name, error))
        return false;
    ParseResult parsed;
    if(!readFile(sourcePath, &parsed, error) || !ensureDirectory(error))
        return false;

    const QString destination = QDir(directoryPath()).filePath(name + QStringLiteral(".dic"));
    if(QFileInfo(destination).exists() || QFileInfo(destination).isSymLink())
        return fail(error, QObject::tr("A dictionary with this name already exists. Choose another name."));
    if(!writeFile(destination, parsed.keys, error))
        return false;

    if(imported)
    {
        imported->name = name;
        imported->absolutePath = destination;
        imported->keyCount = parsed.keys.size();
        imported->duplicateCount = parsed.duplicateCount;
    }
    return true;
}

bool LocalDictionary::exportFile(const QString& sourcePath, const QString& destinationPath, QString* error)
{
    ParseResult parsed;
    if(!readFile(sourcePath, &parsed, error))
        return false;
    return writeFile(destinationPath, parsed.keys, error);
}
