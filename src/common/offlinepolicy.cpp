#include "offlinepolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>
#include <QVector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#endif

namespace {

bool reject(QString *error, const char *message)
{
    if (error)
        *error = QString::fromLatin1(message);
    return false;
}

void clearResult(QString *result)
{
    if (result)
        result->clear();
}

bool hasInvisibleCharacters(const QString &value)
{
    for (QChar ch : value) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f
                || ch.category() == QChar::Other_Control
                || ch.category() == QChar::Other_Format
                || (ch.isSpace() && ch != QLatin1Char(' ')))
            return true;
    }
    return false;
}

bool isDrivePath(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z]:[/\\\\]"));
    return pattern.match(value).hasMatch();
}

bool localPathSyntax(const QString &path, QString *error)
{
    if (path.isEmpty() || path != path.trimmed() || hasInvisibleCharacters(path))
        return reject(error, "Select a local path without surrounding whitespace or control characters.");

    QString slashes = path;
    slashes.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (slashes.startsWith(QStringLiteral("//")))
        return reject(error, "UNC, network shares and device-namespace paths are disabled in offline mode.");
    const int colon = path.indexOf(QLatin1Char(':'));
    if (colon >= 0 && (!isDrivePath(path) || colon != 1
                      || path.indexOf(QLatin1Char(':'), colon + 1) >= 0))
        return reject(error, "URLs, drive-relative paths and alternate data streams are not local file paths.");
    if (!QDir::isAbsolutePath(path))
        return reject(error, "Select an absolute local file path; PATH lookup and relative executables are disabled.");
#ifndef Q_OS_WIN
    if (path.contains(QLatin1Char('\\')) || isDrivePath(path))
        return reject(error, "The selected path is not an absolute path on this operating system.");
#else
    // Win32 can silently trim trailing periods/spaces and resolve device names.
    const QStringList components = slashes.split(QLatin1Char('/'));
    for (int i = 1; i < components.size(); ++i) {
        const QString part = components.at(i);
        if (part != QStringLiteral(".") && part != QStringLiteral("..")
                && (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' '))))
            return reject(error, "Ambiguous Windows path components ending in a period or space are disabled.");
    }
#endif
    return true;
}

bool localVolume(const QString &path, QString *error)
{
#ifdef Q_OS_WIN
    if (!isDrivePath(path))
        return reject(error, "Only local Windows drive paths are supported.");
    QString root = path.left(3);
    root.replace(QLatin1Char('/'), QLatin1Char('\\'));
    const UINT type = GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()));
    if (type == DRIVE_REMOTE || type == DRIVE_UNKNOWN || type == DRIVE_NO_ROOT_DIR)
        return reject(error, "Network drives and unresolved storage devices are disabled in offline mode.");
#else
    const QStorageInfo storage(path);
    if (!storage.isValid() || !storage.isReady())
        return reject(error, "The local storage device could not be verified.");
    const QByteArray type = storage.fileSystemType().toLower();
    static const QSet<QByteArray> networkTypes = {
        "nfs", "nfs4", "cifs", "smb", "smb3", "smbfs", "afp", "afpfs",
        "9p", "davfs", "davfs2", "coda", "ceph", "glusterfs", "lustre"
    };
    // FUSE drivers can be network-backed; fail closed for this initial profile.
    if (networkTypes.contains(type) || type.startsWith("fuse"))
        return reject(error, "Network-backed and FUSE storage are disabled in this offline profile.");
#endif
    return true;
}

bool resolvedLocalFile(const QString &input, QString *canonicalPath, QString *error)
{
    if (!localPathSyntax(input, error) || !localVolume(input, error))
        return false;
    const QFileInfo info(input);
    if (!info.exists() || !info.isFile())
        return reject(error, "The selected path must be an existing regular file.");
    QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty())
        return reject(error, "The selected file's real path could not be resolved.");
#ifdef Q_OS_WIN
    // QFileInfo alone is not sufficient for all junction/reparse-point cases.
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(input.utf16()),
                                FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return reject(error, "The selected file's final Windows path could not be opened.");
    const DWORD length = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    std::vector<wchar_t> buffer(length ? length + 1 : 1);
    const DWORD written = length ? GetFinalPathNameByHandleW(handle, buffer.data(),
                            static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED) : 0;
    CloseHandle(handle);
    if (!written || written >= buffer.size())
        return reject(error, "The selected file's final Windows path could not be resolved.");
    resolved = QString::fromWCharArray(buffer.data(), static_cast<int>(written));
    if (resolved.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive))
        return reject(error, "The selected file resolves to a network share.");
    if (resolved.startsWith(QStringLiteral("\\\\?\\")))
        resolved.remove(0, 4);
    resolved = QDir::fromNativeSeparators(resolved);
#endif
    if (!localPathSyntax(resolved, error) || !localVolume(resolved, error))
        return false;
    if (canonicalPath)
        *canonicalPath = resolved;
    return true;
}

bool forbiddenExecutableName(const QString &path)
{
    const QFileInfo info(path);
    static const QSet<QString> scriptExtensions = {
        QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("ps1"),
        QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"),
        QStringLiteral("fish"), QStringLiteral("py"), QStringLiteral("pyw"),
        QStringLiteral("lua"), QStringLiteral("pl"), QStringLiteral("rb"),
        QStringLiteral("js"), QStringLiteral("jse"), QStringLiteral("vbs"),
        QStringLiteral("vbe"), QStringLiteral("wsf"), QStringLiteral("wsh"),
        QStringLiteral("hta"), QStringLiteral("lnk"), QStringLiteral("url"),
        QStringLiteral("desktop"), QStringLiteral("appref-ms"), QStringLiteral("dll")
    };
    if (scriptExtensions.contains(info.suffix().toLower()))
        return true;
    QString name = info.fileName().toLower();
    if (name.endsWith(QStringLiteral(".exe")))
        name.chop(4);
    static const QRegularExpression launcher(QStringLiteral(
        "^(cmd|command|powershell|pwsh|sh|bash|dash|zsh|fish|csh|tcsh|ksh|"
        "python[0-9]*(\\.[0-9]+)*|perl|ruby|node|nodejs|wscript|cscript|mshta|"
        "rundll32|regsvr32|wsl|wine|wine64|open|xdg-open|osascript|env|busybox)$"));
    return launcher.match(name).hasMatch();
}

bool nativeExecutableFormat(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return reject(error, "The executable could not be read for native-format validation.");
    const QByteArray header = file.read(64);
    if (header.size() < 64)
        return reject(error, "A native executable is required; scripts and wrapper files are disabled.");
#ifdef Q_OS_WIN
    if (!header.startsWith("MZ"))
        return reject(error, "Select the native Proxmark3 executable, not a script or launcher.");
    const unsigned char *h = reinterpret_cast<const unsigned char *>(header.constData());
    const quint32 offset = quint32(h[60]) | (quint32(h[61]) << 8)
                         | (quint32(h[62]) << 16) | (quint32(h[63]) << 24);
    if (offset < 64 || offset > 16 * 1024 * 1024 || !file.seek(offset))
        return reject(error, "The selected file does not have a valid Windows PE header.");
    const QByteArray pe = file.read(24);
    if (pe.size() != 24 || pe.left(4) != QByteArray("PE\0\0", 4)
            || (static_cast<unsigned char>(pe.at(23)) & 0x20))
        return reject(error, "A Windows executable, rather than a DLL or script, is required.");
#elif defined(Q_OS_MAC)
    const QByteArray magic = header.left(4).toHex();
    if (magic != "feedface" && magic != "cefaedfe" && magic != "feedfacf"
            && magic != "cffaedfe" && magic != "cafebabe" && magic != "bebafeca"
            && magic != "cafebabf" && magic != "bfbafeca")
        return reject(error, "A native Mach-O executable is required; shell scripts are disabled.");
#else
    if (header.left(4) != QByteArray("\x7f" "ELF", 4))
        return reject(error, "A native ELF executable is required; shell scripts are disabled.");
#endif
#ifndef Q_OS_WIN
    if (!QFileInfo(path).isExecutable())
        return reject(error, "The selected native file does not have execute permission.");
#endif
    return true;
}

struct Token {
    QString value;
    bool quoted;
};

bool tokenize(const QString &command, QVector<Token> *tokens, QString *error)
{
    int pos = 0;
    while (pos < command.size()) {
        if (command.at(pos) == QLatin1Char(' ')) {
            ++pos;
            continue;
        }
        const QChar first = command.at(pos);
        const bool quoted = first == QLatin1Char('"') || first == QLatin1Char('\'');
        const int start = quoted ? ++pos : pos;
        if (quoted) {
            while (pos < command.size() && command.at(pos) != first)
                ++pos;
            if (pos == command.size())
                return reject(error, "The command contains an unmatched quotation mark.");
            tokens->append(Token{command.mid(start, pos - start), true});
            ++pos;
            if (pos < command.size() && command.at(pos) != QLatin1Char(' '))
                return reject(error, "Quoted arguments must be separate tokens; quote the entire filename.");
        } else {
            while (pos < command.size() && command.at(pos) != QLatin1Char(' ')) {
                if (command.at(pos) == QLatin1Char('"') || command.at(pos) == QLatin1Char('\''))
                    return reject(error, "Quoted arguments must be separate tokens; quote the entire filename.");
                ++pos;
            }
            tokens->append(Token{command.mid(start, pos - start), false});
        }
    }
    return true;
}

const QSet<QString> &allowedCommands()
{
    // Exact LEAF paths, never a prefix allowlist. RRG CmdsParse recursively
    // supports !system(), lowercase matching and unique prefix abbreviations;
    // proxmark3.c splits ';' BEFORE command parsing, even inside quoted text.
    // Reviewed against upstream 0bf02faa49649e51e07896e42943beaf0a38e334 and
    // this GUI's official / RRG v4.13441, v4.15864 and v4.16717 JSON profiles.
    // New commands require review here; arbitrary future hf/lf subcommands are
    // intentionally not trusted. Arguments remain the backend's responsibility.
    static const QSet<QString> commands = {
        QStringLiteral("help"), QStringLiteral("clear"), QStringLiteral("quit"),
        QStringLiteral("exit"), QStringLiteral("hints"), QStringLiteral("msleep"),
        QStringLiteral("rem"), QStringLiteral("auto"),
        QStringLiteral("prefs show"), QStringLiteral("prefs help"),
        QStringLiteral("hw help"), QStringLiteral("hw version"), QStringLiteral("hw status"),
        QStringLiteral("hw tune"), QStringLiteral("hw setlfdivisor"),
        QStringLiteral("hw ping"), QStringLiteral("hw dbg"), QStringLiteral("hw reset"),
        QStringLiteral("hf help"), QStringLiteral("hf search"), QStringLiteral("hf sniff"),
        QStringLiteral("hf tune"), QStringLiteral("hf list"),
        QStringLiteral("hf 14a help"), QStringLiteral("hf 14a info"),
        QStringLiteral("hf 14a reader"), QStringLiteral("hf 14a raw"),
        QStringLiteral("hf 14a sniff"), QStringLiteral("hf 14a snoop"), QStringLiteral("hf 14a sim"),
        QStringLiteral("hf 14b help"), QStringLiteral("hf 14b info"),
        QStringLiteral("hf 14b reader"), QStringLiteral("hf 14b raw"),
        QStringLiteral("hf 14b sniff"), QStringLiteral("hf 14b snoop"),
        QStringLiteral("hf 15 help"), QStringLiteral("hf 15 info"),
        QStringLiteral("hf 15 reader"), QStringLiteral("hf 15 raw"), QStringLiteral("hf 15 sniff"),
        QStringLiteral("hf mf help"), QStringLiteral("hf mf info"), QStringLiteral("hf mf chk"),
        QStringLiteral("hf mf fchk"), QStringLiteral("hf mf nested"),
        QStringLiteral("hf mf staticnested"), QStringLiteral("hf mf hardnested"),
        QStringLiteral("hf mf darkside"), QStringLiteral("hf mf mifare"),
        QStringLiteral("hf mf dump"), QStringLiteral("hf mf restore"),
        QStringLiteral("hf mf eclr"), QStringLiteral("hf mf cwipe"),
        QStringLiteral("hf mf egetblk"), QStringLiteral("hf mf eget"),
        QStringLiteral("hf mf cgetblk"), QStringLiteral("hf mf rdbl"),
        QStringLiteral("hf mf rdsc"), QStringLiteral("hf mf cgetsc"),
        QStringLiteral("hf mf wrbl"), QStringLiteral("hf mf csetblk"),
        QStringLiteral("hf mf esetblk"), QStringLiteral("hf mf eset"),
        QStringLiteral("hf mf csetuid"), QStringLiteral("hf mf sim"),
        QStringLiteral("hf mf sniff"), QStringLiteral("hf mf view"),
        QStringLiteral("hf mf eload"), QStringLiteral("hf mf esave"),
        QStringLiteral("hf mf ecfill"),
        QStringLiteral("lf help"), QStringLiteral("lf read"), QStringLiteral("lf sniff"),
        QStringLiteral("lf snoop"), QStringLiteral("lf search"), QStringLiteral("lf tune"),
        QStringLiteral("lf config"),
        QStringLiteral("lf em 410x reader"), QStringLiteral("lf em 410x read"),
        QStringLiteral("lf em 410x watch"), QStringLiteral("lf em 410x sim"),
        QStringLiteral("lf em 410x clone"), QStringLiteral("lf em 410x help"),
        QStringLiteral("lf em 410xwrite"), QStringLiteral("lf em 410xread"),
        QStringLiteral("lf t55xx help"), QStringLiteral("lf t55xx detect"),
        QStringLiteral("lf t55xx info"), QStringLiteral("lf t55xx read"),
        QStringLiteral("lf t55xx write"), QStringLiteral("lf t55xx dump"),
        QStringLiteral("lf t55xx restore"), QStringLiteral("lf t55xx wipe"),
        QStringLiteral("lf t55xx config"), QStringLiteral("lf t55xx trace"),
        QStringLiteral("lf t55xx resetread"),
        QStringLiteral("data help"), QStringLiteral("data plot"), QStringLiteral("data hide"),
        QStringLiteral("data clear"), QStringLiteral("data load"), QStringLiteral("data save"),
        QStringLiteral("data print"), QStringLiteral("data samples"),
        QStringLiteral("data rawdemod"), QStringLiteral("data detectclock"),
        QStringLiteral("data autocorr"), QStringLiteral("data decimate"),
        QStringLiteral("data undecimate"), QStringLiteral("data norm"),
        QStringLiteral("data ltrim"), QStringLiteral("data rtrim"),
        QStringLiteral("trace help"), QStringLiteral("trace list"),
        QStringLiteral("trace load"), QStringLiteral("trace save")
    };
    return commands;
}

bool categoryHelpOnly(const QVector<Token> &tokens)
{
    static const QSet<QString> categories = {
        QStringLiteral("hw"), QStringLiteral("hf"), QStringLiteral("lf"),
        QStringLiteral("data"), QStringLiteral("trace"), QStringLiteral("prefs"),
        QStringLiteral("hf mf"), QStringLiteral("hf 14a"), QStringLiteral("hf 14b"),
        QStringLiteral("hf 15"), QStringLiteral("lf em"),
        QStringLiteral("lf em 410x"), QStringLiteral("lf t55xx")
    };
    QStringList words;
    for (int i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens.at(i);
        if (token.quoted)
            return false;
        if (i == tokens.size() - 1
                && (token.value == QStringLiteral("-h") || token.value == QStringLiteral("--help")))
            break;
        words.append(token.value.toLower());
    }
    // A category is allowed ONLY when it is the entire input, optionally with
    // a help flag. Treating "hf" as an allowed leaf would admit all its children.
    return categories.contains(words.join(QLatin1Char(' ')));
}

bool localArgument(const QString &argument, QString *error)
{
    QString value = argument;
    if (value.startsWith(QLatin1Char('-')) && value.contains(QLatin1Char('=')))
        value = value.mid(value.indexOf(QLatin1Char('=')) + 1);
    QString slashes = value;
    slashes.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // Also catches attached-option UNC paths (-f\\server), mixed separators,
    // device namespace paths, and URL schemes even inside a quoted argument.
    if (slashes.contains(QStringLiteral("//")))
        return reject(error, "URLs, UNC paths and network-share arguments are disabled.");
    const int colon = value.indexOf(QLatin1Char(':'));
    if (colon >= 0 && (!isDrivePath(value) || colon != 1
                      || value.indexOf(QLatin1Char(':'), colon + 1) >= 0))
        return reject(error, "Network schemes, drive-relative paths and alternate data streams are disabled.");
#ifdef Q_OS_WIN
    if (isDrivePath(value) && !localVolume(value, error))
        return false;
#endif
    return true;
}

} // namespace

bool OfflinePolicy::validateLocalFilePath(const QString &input, QString *canonicalPath, QString *error)
{
    clearResult(canonicalPath);
    clearResult(error);
    return resolvedLocalFile(input, canonicalPath, error);
}

bool OfflinePolicy::validateExecutablePath(const QString &input, QString *canonicalPath, QString *error)
{
    clearResult(canonicalPath);
    clearResult(error);
    if (forbiddenExecutableName(input))
        return reject(error, "Scripts, command interpreters and launcher executables are disabled.");
    QString resolved;
    if (!resolvedLocalFile(input, &resolved, error))
        return false;
    if (forbiddenExecutableName(resolved))
        return reject(error, "The selected file resolves to a script, interpreter or launcher.");
    if (!nativeExecutableFormat(resolved, error))
        return false;
    if (canonicalPath)
        *canonicalPath = resolved;
    return true;
}

bool OfflinePolicy::validateSerialPort(const QString &port, QString *normalizedPort, QString *error)
{
    clearResult(normalizedPort);
    clearResult(error);
    if (port.isEmpty() || port != port.trimmed() || hasInvisibleCharacters(port))
        return reject(error, "Serial-port identifiers cannot contain whitespace, line breaks or control characters.");
    static const QRegularExpression com(QStringLiteral("^COM[1-9][0-9]{0,3}$"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression device(QStringLiteral("^/dev/(tty[A-Za-z0-9._-]+|cu\\.[A-Za-z0-9._-]+)$"));
    QString normalized;
    if (com.match(port).hasMatch()) {
        normalized = port.toUpper();
    } else if (device.match(port).hasMatch()) {
        normalized = port;
#ifndef Q_OS_WIN
        const QFileInfo info(port);
        if (info.exists() || info.isSymLink()) {
            normalized = info.canonicalFilePath();
            if (!device.match(normalized).hasMatch())
                return reject(error, "The serial-port link does not resolve to a supported local device.");
        }
#endif
    } else {
        return reject(error, "Select a local COMn, /dev/tty* or /dev/cu.* serial port; network transports are disabled.");
    }
    if (normalizedPort)
        *normalizedPort = normalized;
    return true;
}

bool OfflinePolicy::buildLaunchArguments(const QString &port, QStringList *arguments, QString *error)
{
    if (arguments)
        arguments->clear();
    QString normalized;
    if (!validateSerialPort(port, &normalized, error))
        return false;
    if (arguments)
        *arguments = QStringList{normalized, QStringLiteral("-f")};
    return true;
}

bool OfflinePolicy::validateLaunchArguments(const QStringList &arguments, QString *error)
{
    clearResult(error);
    if (arguments.size() != 2 || arguments.at(1) != QStringLiteral("-f"))
        return reject(error, "Offline startup requires exactly a local serial port and -f; custom arguments are disabled.");
    QString normalized;
    if (!validateSerialPort(arguments.at(0), &normalized, error))
        return false;
    if (normalized != arguments.at(0))
        return reject(error, "Use the normalized local serial-port argument supplied by the offline policy.");
    return true;
}

bool OfflinePolicy::validateCommand(const QString &command, QString *error)
{
    clearResult(error);
    if (hasInvisibleCharacters(command))
        return reject(error, "Commands must be a single line without control characters or hidden Unicode separators.");
    // RRG's piped input uses a 256-byte fgets buffer. Leave room for LF and NUL
    // so truncation cannot turn one GUI input into multiple backend commands.
    if (command.toUtf8().size() > 254)
        return reject(error, "The command exceeds the offline profile's 254-byte single-line limit.");
    const QString shellCharacters = QStringLiteral(";|&<>`$!^%");
    for (QChar ch : command) {
        if (shellCharacters.contains(ch))
            return reject(error, "Command chaining, shell execution, substitutions and redirections are disabled.");
    }
    QVector<Token> tokens;
    if (!tokenize(command, &tokens, error))
        return false;
    if (tokens.isEmpty())
        return true;

    QString path;
    int leafLength = 0;
    static const QRegularExpression commandWord(QStringLiteral("^[A-Za-z0-9]+$"));
    for (int i = 0; i < tokens.size() && i < 4; ++i) {
        const Token &token = tokens.at(i);
        if (token.quoted || !commandWord.match(token.value).hasMatch())
            break;
        if (!path.isEmpty())
            path += QLatin1Char(' ');
        path += token.value.toLower();
        if (allowedCommands().contains(path)) {
            leafLength = i + 1;
            break;
        }
    }
    if (!leafLength && categoryHelpOnly(tokens))
        return true;
    if (!leafLength)
        return reject(error, "This exact command is not in the reviewed offline allowlist; scripts, aliases, abbreviations and network commands are disabled.");
    for (int i = leafLength; i < tokens.size(); ++i) {
        if (!localArgument(tokens.at(i).value, error))
            return false;
    }
    return true;
}

bool OfflinePolicy::normalizeCommandFrame(const QString &frame, QString *command, QString *error)
{
    clearResult(command);
    QString value = frame;
    if (value.endsWith(QStringLiteral("\r\n")))
        value.chop(2);
    else if (value.endsWith(QLatin1Char('\n')) || value.endsWith(QLatin1Char('\r')))
        value.chop(1);
    if (!validateCommand(value, error))
        return false;
    if (command)
        *command = value;
    return true;
}

bool OfflinePolicy::validateCommandTemplate(const QString &commandTemplate, QString *error)
{
    clearResult(error);
    if (commandTemplate.size() > 2048)
        return reject(error, "The command template is too long.");
    static const QHash<QString, QString> placeholders = {
        {QStringLiteral("card type"), QStringLiteral("1k")},
        {QStringLiteral("key type"), QStringLiteral("a")},
        {QStringLiteral("known key type"), QStringLiteral("a")},
        {QStringLiteral("target key type"), QStringLiteral("b")},
        {QStringLiteral("key"), QStringLiteral("FFFFFFFFFFFF")},
        {QStringLiteral("known key"), QStringLiteral("FFFFFFFFFFFF")},
        {QStringLiteral("uid"), QStringLiteral("01020304")},
        {QStringLiteral("id"), QStringLiteral("0102030405")},
        {QStringLiteral("atqa"), QStringLiteral("0400")},
        {QStringLiteral("sak"), QStringLiteral("08")},
        {QStringLiteral("data"), QStringLiteral("00000000000000000000000000000000")},
        {QStringLiteral("filename"), QStringLiteral("offline-data.bin")},
        {QStringLiteral("type"), QString()},
        {QStringLiteral("block"), QStringLiteral("0")},
        {QStringLiteral("known key block"), QStringLiteral("0")},
        {QStringLiteral("target key block"), QStringLiteral("4")},
        {QStringLiteral("sector"), QStringLiteral("0")},
        {QStringLiteral("divisor"), QStringLiteral("95")},
        {QStringLiteral("bits per sample"), QStringLiteral("8")},
        {QStringLiteral("decimation"), QStringLiteral("1")},
        {QStringLiteral("averaging"), QStringLiteral("1")},
        {QStringLiteral("trigger threshold"), QStringLiteral("0")},
        {QStringLiteral("samples to skip"), QStringLiteral("0")}
    };
    static const QRegularExpression placeholder(QStringLiteral("<([^<>]+)>"));
    QString expanded;
    int previous = 0;
    QRegularExpressionMatchIterator matches = placeholder.globalMatch(commandTemplate);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString key = match.captured(1);
        if (!placeholders.contains(key))
            return reject(error, "The template contains an unknown placeholder; extend the reviewed policy explicitly.");
        expanded += commandTemplate.mid(previous, match.capturedStart() - previous);
        expanded += placeholders.value(key);
        previous = match.capturedEnd();
    }
    expanded += commandTemplate.mid(previous);
    return validateCommand(expanded, error);
}
