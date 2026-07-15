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
    explicit ServerTrayController(RemoteServer* _server, QObject* _parent = nullptr);

    void show();
    void runTimedLockTest(int _seconds);

private:
    void refreshActionState();
    void showInfo(const QString& _title, const QString& _message);
    void showError(const QString& _title, const QString& _message);

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
