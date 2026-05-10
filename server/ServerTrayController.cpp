#include "ServerTrayController.h"

#include "CommandService.h"
#include "PlatformIntegration.h"
#include "RemoteServer.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>

ServerTrayController::ServerTrayController(RemoteServer* server, QObject* parent)
    : QObject(parent)
    , m_server(server)
    , m_trayIcon(new QSystemTrayIcon(this))
    , m_menu(new QMenu())
{
    m_trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    connect(this, &QObject::destroyed, m_menu, &QObject::deleteLater);

    m_statusAction = m_menu->addAction(QString());
    m_statusAction->setEnabled(false);

    m_adminAction = m_menu->addAction(tr("Restart as administrator"));
    connect(m_adminAction, &QAction::triggered, this, [this] {
        QString errorMessage;
        if (!PlatformIntegration::relaunchElevated({}, &errorMessage)) {
            showError(tr("Elevation failed"), errorMessage);
            return;
        }
        QCoreApplication::quit();
    });

    m_menu->addSeparator();

    m_startupInstallAction = m_menu->addAction(tr("Enable startup"));
    connect(m_startupInstallAction, &QAction::triggered, this, [this] {
        QString errorMessage;
        if (!PlatformIntegration::installStartupEntry(&errorMessage)) {
            showError(tr("Startup"), errorMessage);
            return;
        }
        refreshActionState();
        showInfo(tr("Startup"), tr("Startup has been enabled."));
    });

    m_startupRemoveAction = m_menu->addAction(tr("Disable startup"));
    connect(m_startupRemoveAction, &QAction::triggered, this, [this] {
        QString errorMessage;
        if (!PlatformIntegration::removeStartupEntry(&errorMessage)) {
            showError(tr("Startup"), errorMessage);
            return;
        }
        refreshActionState();
        showInfo(tr("Startup"), tr("Startup has been disabled."));
    });

    m_menu->addSeparator();

    m_lockAction = m_menu->addAction(tr("Lock machine"));
    connect(m_lockAction, &QAction::triggered, this, [this] {
        m_server->commandService()->lockLocalMachine();
        refreshActionState();
    });

    m_unlockAction = m_menu->addAction(tr("Unlock machine"));
    connect(m_unlockAction, &QAction::triggered, this, [this] {
        m_server->commandService()->unlockLocalMachine();
        refreshActionState();
    });

    m_lockTestAction = m_menu->addAction(tr("Run 5s lock test"));
    connect(m_lockTestAction, &QAction::triggered, this, [this] {
        runTimedLockTest(5);
    });

    m_menu->addSeparator();

    m_quitAction = m_menu->addAction(tr("Exit"));
    connect(m_quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_trayIcon->setContextMenu(m_menu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showInfo(tr("Remote Server"), tr("Server is listening on port %1.").arg(m_server->listeningPort()));
        }
    });

    refreshActionState();
}

void ServerTrayController::show()
{
    m_trayIcon->show();
    showInfo(tr("Remote Server"), tr("Server is listening on port %1.").arg(m_server->listeningPort()));
}

void ServerTrayController::runTimedLockTest(int seconds)
{
    m_server->commandService()->lockLocalMachine();
    refreshActionState();
    QTimer::singleShot(seconds * 1000, this, [this] {
        m_server->commandService()->unlockLocalMachine();
        refreshActionState();
        showInfo(tr("Lock test"), tr("Timed lock test finished."));
    });
}

void ServerTrayController::refreshActionState()
{
    m_statusAction->setText(tr("Listening on port %1").arg(m_server->listeningPort()));
    const bool isAdmin = PlatformIntegration::isRunningAsAdmin();
    m_adminAction->setEnabled(!isAdmin);
    m_adminAction->setText(isAdmin ? tr("Already running as administrator") : tr("Restart as administrator"));

    const bool startupEnabled = PlatformIntegration::startupEntryExists();
    m_startupInstallAction->setEnabled(!startupEnabled);
    m_startupRemoveAction->setEnabled(startupEnabled);

    const bool locked = m_server->commandService()->isLocked();
    m_lockAction->setEnabled(!locked);
    m_unlockAction->setEnabled(locked);
}

void ServerTrayController::showInfo(const QString& title, const QString& message)
{
    if (m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 3000);
    }
}

void ServerTrayController::showError(const QString& title, const QString& message)
{
    QMessageBox::warning(nullptr, title, message);
}
