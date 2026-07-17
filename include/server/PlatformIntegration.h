#pragma once

#include "common/Protocol.h"

#include <QPoint>
#include <QString>
#include <QStringList>

class QImage;

/** @brief Isolates Windows-specific process, input, shell, and startup operations. */
class PlatformIntegration
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
     */
    static void setSystemUiLocked(bool _locked);

    /**
     * @brief Captures the primary Windows screen without GUI-thread resources.
     * @return Captured screen image, or a null image when capture fails.
     */
    [[nodiscard]] static QImage capturePrimaryScreen();

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
