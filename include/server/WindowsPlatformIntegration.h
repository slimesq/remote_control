#pragma once

#include "common/Protocol.h"

#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QStringList>

/** @brief Isolates Windows-specific process, input, shell, and startup operations. */
class WindowsPlatformIntegration
{
public:
    /**
     * @brief Moves the cursor and injects a global mouse action.
     * @param _position Absolute screen position.
     * @param _action Mouse action to inject.
     * @param _button Mouse button, or None for movement only.
     * @return true when Windows accepts the requested input; otherwise false.
     */
    [[nodiscard]] static bool sendGlobalMouseEvent(QPoint const& _position,
                                                   remote_control::MouseAction _action,
                                                   remote_control::MouseButton _button);

    /**
     * @brief Applies or restores the Windows lock-overlay state.
     * @param _locked Whether the system UI should be locked.
     * @return true when the cursor confinement state is updated; otherwise false.
     */
    [[nodiscard]] static bool setSystemUiLocked(bool _locked);

    /**
     * @brief Captures and PNG-encodes the primary screen without an intermediate image copy.
     * @return Encoded PNG bytes, or an empty array when capture or encoding fails.
     */
    [[nodiscard]] static QByteArray capturePrimaryScreenPng();

    /**
     * @brief Checks whether a path belongs to a directly attached Windows drive.
     * @param _path Absolute file-system path to validate.
     * @return true for supported local drive paths; otherwise false.
     */
    [[nodiscard]] static bool isLocalFilePath(QString const& _path);

    /**
     * @brief Opens a local file through the Windows shell association.
     * @param _path Local file-system path to open.
     * @return true when Windows accepts the open request; otherwise false.
     */
    [[nodiscard]] static bool openLocalFile(QString const& _path);

    /**
     * @brief Returns whether the process has administrator privileges.
     * @return true when the process is elevated; otherwise false.
     */
    [[nodiscard]] static bool isRunningAsAdmin();

    /**
     * @brief Relaunches the current executable through Windows UAC.
     * @param _arguments Arguments passed to the elevated process.
     * @param _errorMessage Optional output for a user-facing failure message.
     * @return true when Windows accepts the launch request; otherwise false.
     */
    [[nodiscard]] static bool relaunchElevated(QStringList const& _arguments,
                                               QString* _errorMessage = nullptr);

    /**
     * @brief Waits for an earlier server process to release its resources.
     * @param _processId Windows process identifier to wait for.
     * @param _timeoutMs Maximum wait duration in milliseconds.
     * @param _errorMessage Optional output for a user-facing failure message.
     * @return true when the process has exited or no longer exists; otherwise false.
     */
    [[nodiscard]] static bool waitForProcessExit(quint32 _processId,
                                                 int _timeoutMs,
                                                 QString* _errorMessage = nullptr);

    /**
     * @brief Adds the server to the current user's startup entries.
     * @param _errorMessage Optional output for a user-facing failure message.
     * @return true when the entry is written successfully; otherwise false.
     */
    [[nodiscard]] static bool installStartupEntry(QString* _errorMessage = nullptr);

    /**
     * @brief Removes the server from the current user's startup entries.
     * @param _errorMessage Optional output for a user-facing failure message.
     * @return true when the entry is removed successfully; otherwise false.
     */
    [[nodiscard]] static bool removeStartupEntry(QString* _errorMessage = nullptr);

    /**
     * @brief Returns whether the current user's startup entry exists.
     * @return true when the startup entry exists; otherwise false.
     */
    [[nodiscard]] static bool startupEntryExists();

private:
    /**
     * @brief Returns the current-user startup registry path.
     * @return Native registry path used by QSettings.
     */
    [[nodiscard]] static QString startupRegistryPath();

    /**
     * @brief Returns the server's startup registry value name.
     * @return Startup registry value name.
     */
    [[nodiscard]] static QString startupValueName();

    /**
     * @brief Builds the command stored in the startup entry.
     * @return Quoted native path to the current executable.
     */
    [[nodiscard]] static QString startupCommand();
};
