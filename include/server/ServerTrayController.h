#pragma once

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;
class RemoteServer;

/** @brief Exposes server state and local maintenance actions through a tray icon. */
class ServerTrayController : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Creates a tray controller for a running server.
     * @param _server Running server controlled by the tray actions.
     * @param _parent Parent object, or nullptr.
     */
    explicit ServerTrayController(RemoteServer* _server, QObject* _parent = nullptr);

    /** @brief Shows the tray icon and listening-port notification. */
    void show();

private:
    /** @brief Refreshes actions from current server state. */
    void refreshActionState();

    /**
     * @brief Shows an informational tray message.
     * @param _title Message title.
     * @param _message Message body.
     */
    void showInfo(QString const& _title, QString const& _message);

    /**
     * @brief Shows an error tray message.
     * @param _title Message title.
     * @param _message Message body.
     */
    void showError(QString const& _title, QString const& _message);

    RemoteServer* m_server{nullptr};       ///< Non-owning server controlled by the tray interface.
    QSystemTrayIcon* m_trayIcon{nullptr};  ///< Parent-owned system tray icon.
    QMenu* m_menu{nullptr};                ///< Context menu released with this controller.
    QAction* m_statusAction{nullptr};      ///< Read-only server status action.
    QAction* m_adminAction{nullptr};       ///< Restarts the server with elevation.
    QAction* m_startupInstallAction{nullptr};  ///< Enables startup for the current user.
    QAction* m_startupRemoveAction{nullptr};   ///< Disables startup for the current user.
    QAction* m_lockAction{nullptr};            ///< Locks the local machine simulation.
    QAction* m_unlockAction{nullptr};          ///< Unlocks the local machine simulation.
    QAction* m_lockTestAction{nullptr};        ///< Starts a timed lock test.
    QAction* m_quitAction{nullptr};            ///< Exits the server application.
};
