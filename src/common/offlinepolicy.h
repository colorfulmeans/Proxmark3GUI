#ifndef OFFLINEPOLICY_H
#define OFFLINEPOLICY_H

#include <QString>
#include <QStringList>

// Input policy for this GUI, NOT an operating-system sandbox or a statement of
// trust in a selected executable. An unknown/replaced native backend can ignore
// these inputs and can itself access files, start processes or use the network.
// Use a reviewed backend and an OS network policy for stronger isolation.
//
// Call at the final process-launch/write boundary, including reconnects. Do not
// rely on validation when a widget/configuration was originally loaded. The
// caller must start the returned canonical path with QProcess, never a shell.
class OfflinePolicy
{
public:
    static bool validateExecutablePath(const QString &input,
                                       QString *canonicalPath = nullptr,
                                       QString *error = nullptr);

    // Existing local regular file, for a user-selected JSON configuration.
    // Rejects URLs, UNC/device paths and known network-backed volumes; resolves
    // symlinks. This is not a content/schema validator or a race-free file open.
    static bool validateLocalFilePath(const QString &input,
                                     QString *canonicalPath = nullptr,
                                     QString *error = nullptr);

    // Identifier validation only: COMn or a local /dev/tty* /dev/cu.* port.
    // It does not establish that the selected port is a genuine USB PM3.
    static bool validateSerialPort(const QString &port,
                                   QString *normalizedPort = nullptr,
                                   QString *error = nullptr);

    // The only launch form admitted is {normalizedLocalPort, "-f"}. No command,
    // script, flash, network-listen or other pre-execution arguments are accepted.
    static bool buildLaunchArguments(const QString &port, QStringList *arguments,
                                     QString *error = nullptr);
    static bool validateLaunchArguments(const QStringList &arguments,
                                        QString *error = nullptr);

    // One command with NO line terminator. Empty/space-only input is permitted
    // for local interaction. A reviewed exact command path is required: no
    // prefix abbreviations, aliases, scripts, network transports, or shell.
    // Arguments are not a complete PM3 option/type validator. Local card/file
    // writes are not made read-only by this policy. Filenames remain data.
    static bool validateCommand(const QString &command, QString *error = nullptr);

    // For the QProcess::write boundary: remove at most ONE trailing LF, CR or
    // CRLF, then apply validateCommand. The caller writes *command + '\n'.
    // This rejects embedded newlines and multiple trailing command frames.
    static bool normalizeCommandFrame(const QString &frame, QString *command,
                                      QString *error = nullptr);

    // Checks the known placeholders used by this repository's JSON templates.
    // Always validate the fully substituted command again at execution time.
    static bool validateCommandTemplate(const QString &commandTemplate,
                                        QString *error = nullptr);

private:
    OfflinePolicy();
};

#endif // OFFLINEPOLICY_H
