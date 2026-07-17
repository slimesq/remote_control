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
    /** @brief Creates a tray controller for a running server. */
    explicit ServerTrayController(RemoteServer* _server, QObject* _parent = nullptr);

    /** @brief Shows the tray icon and listening-port notification. */
    void show();

private:
    void refreshActionState();
    void showInfo(QString const& _title, QString const& _message);
    void showError(QString const& _title, QString const& _message);

    RemoteServer* m_server{nullptr};
    QSystemTrayIcon* m_trayIcon{nullptr};
    QMenu* m_menu{nullptr};
    QAction* m_statusAction{nullptr};
    QAction* m_adminAction{nullptr};
    QAction* m_startupInstallAction{nullptr};
    QAction* m_startupRemoveAction{nullptr};
    QAction* m_lockAction{nullptr};
    QAction* m_unlockAction{nullptr};
    QAction* m_lockTestAction{nullptr};
    QAction* m_quitAction{nullptr};
};
