#pragma once

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;
class RemoteServer;

class ServerTrayController : public QObject
{
    Q_OBJECT

public:
    explicit ServerTrayController(RemoteServer* server, QObject* parent = nullptr);

    void show();
    void runTimedLockTest(int seconds);

private:
    void refreshActionState();
    void showInfo(const QString& title, const QString& message);
    void showError(const QString& title, const QString& message);

    RemoteServer* m_server = nullptr;
    QSystemTrayIcon* m_trayIcon = nullptr;
    QMenu* m_menu = nullptr;
    QAction* m_statusAction = nullptr;
    QAction* m_adminAction = nullptr;
    QAction* m_startupInstallAction = nullptr;
    QAction* m_startupRemoveAction = nullptr;
    QAction* m_lockAction = nullptr;
    QAction* m_unlockAction = nullptr;
    QAction* m_lockTestAction = nullptr;
    QAction* m_quitAction = nullptr;
};
