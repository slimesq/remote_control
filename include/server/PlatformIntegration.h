#pragma once

#include "common/Protocol.h"

#include <QPoint>
#include <QString>
#include <QStringList>

/** @brief Isolates Windows-specific process, input, shell, and startup operations. */
class PlatformIntegration
{
public:
    /**
     * @brief Moves the system cursor and optionally injects a global mouse-button action.
     * @param _position Absolute screen position for the event.
     * @param _action Mouse action to perform.
     * @param _button Mouse button to inject, or None for a move-only event.
     * @return true when the event is valid and Windows accepts all requested input; otherwise
     * false.
     */
    [[nodiscard]] static bool sendGlobalMouseEvent(QPoint const& _position,
                                                   remote_control::MouseAction _action,
                                                   remote_control::MouseButton _button);

    /**
     * @brief Hides or restores supported Windows shell UI and cursor movement for the lock overlay.
     * @param _locked true to hide and constrain system UI; false to restore it.
     */
    static void setSystemUiLocked(bool _locked);

    /**
     * @brief Checks whether the current process has administrator privileges.
     * @return true when the process is running as an administrator; otherwise
     * false.
     */
    [[nodiscard]] static bool isRunningAsAdmin();

    /**
     * @brief Starts a new instance of the current executable through Windows UAC.
     * @param _arguments Command-line arguments passed to the elevated process.
     * @param _errorMessage Optional output for a user-facing failure message.
     * @return true when Windows accepts the launch request; otherwise false.
     */
    [[nodiscard]] static bool relaunchElevated(QStringList const& _arguments,
                                               QString* _errorMessage = nullptr);

    /**
     * @brief Adds the current executable to the current user's Windows startup
     * entries.
     * @param _errorMessage Optional output for a user-facing failure message.
     * @return true when the startup entry is written successfully; otherwise
     * false.
     */
    [[nodiscard]] static bool installStartupEntry(QString* _errorMessage = nullptr);

    /**
     * @brief Removes the server from the current user's Windows startup entries.
     * @param _errorMessage Optional output for a user-facing failure message.
     * @return true when the startup entry is removed successfully; otherwise
     * false.
     */
    [[nodiscard]] static bool removeStartupEntry(QString* _errorMessage = nullptr);

    /**
     * @brief Checks whether the current user's Windows startup entry exists.
     * @return true when the startup entry exists; otherwise false.
     */
    [[nodiscard]] static bool startupEntryExists();

private:
    /**
     * @brief Returns the registry path containing current-user startup entries.
     * @return Native Windows registry path used by QSettings.
     */
    [[nodiscard]] static QString startupRegistryPath();

    /**
     * @brief Returns the registry value name used by this server.
     * @return Startup registry value name.
     */
    [[nodiscard]] static QString startupValueName();

    /**
     * @brief Builds the command stored in the Windows startup entry.
     * @return Quoted absolute path to the current executable.
     */
    [[nodiscard]] static QString startupCommand();
};
