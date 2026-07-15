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

ServerTrayController::ServerTrayController(RemoteServer* _server, QObject* _parent)
    : QObject(_parent)
    , m_server(_server)
    , m_trayIcon(new QSystemTrayIcon(this))
    , m_menu(new QMenu())
{
    this->m_trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    connect(this, &QObject::destroyed, this->m_menu, &QObject::deleteLater);

    this->m_statusAction = this->m_menu->addAction(QString());
    this->m_statusAction->setEnabled(false);

    this->m_adminAction = this->m_menu->addAction(tr("Restart as administrator"));
    connect(this->m_adminAction, &QAction::triggered, this, [this] {
        QString errorMessage;
        if (!PlatformIntegration::relaunchElevated({}, &errorMessage)) {
            this->showError(tr("Elevation failed"), errorMessage);
            return;
        }
        QCoreApplication::quit();
    });

    this->m_menu->addSeparator();

    this->m_startupInstallAction = this->m_menu->addAction(tr("Enable startup"));
    connect(this->m_startupInstallAction, &QAction::triggered, this, [this] {
        QString errorMessage;
        if (!PlatformIntegration::installStartupEntry(&errorMessage)) {
            this->showError(tr("Startup"), errorMessage);
            return;
        }
        this->refreshActionState();
        this->showInfo(tr("Startup"), tr("Startup has been enabled."));
    });

    this->m_startupRemoveAction = this->m_menu->addAction(tr("Disable startup"));
    connect(this->m_startupRemoveAction, &QAction::triggered, this, [this] {
        QString errorMessage;
        if (!PlatformIntegration::removeStartupEntry(&errorMessage)) {
            this->showError(tr("Startup"), errorMessage);
            return;
        }
        this->refreshActionState();
        this->showInfo(tr("Startup"), tr("Startup has been disabled."));
    });

    this->m_menu->addSeparator();

    this->m_lockAction = this->m_menu->addAction(tr("Lock machine"));
    connect(this->m_lockAction, &QAction::triggered, this, [this] {
        this->m_server->commandService()->lockLocalMachine();
        this->refreshActionState();
    });

    this->m_unlockAction = this->m_menu->addAction(tr("Unlock machine"));
    connect(this->m_unlockAction, &QAction::triggered, this, [this] {
        this->m_server->commandService()->unlockLocalMachine();
        this->refreshActionState();
    });

    this->m_lockTestAction = this->m_menu->addAction(tr("Run 5s lock test"));
    connect(this->m_lockTestAction, &QAction::triggered, this, [this] {
        this->runTimedLockTest(5);
    });

    this->m_menu->addSeparator();

    this->m_quitAction = this->m_menu->addAction(tr("Exit"));
    connect(this->m_quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    this->m_trayIcon->setContextMenu(this->m_menu);
    connect(this->m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason _reason) {
        if (_reason == QSystemTrayIcon::Trigger || _reason == QSystemTrayIcon::DoubleClick) {
            this->showInfo(tr("Remote Server"), tr("Server is listening on port %1.").arg(this->m_server->listeningPort()));
        }
    });

    this->refreshActionState();
}

void ServerTrayController::show()
{
    this->m_trayIcon->show();
    this->showInfo(tr("Remote Server"), tr("Server is listening on port %1.").arg(this->m_server->listeningPort()));
}

void ServerTrayController::runTimedLockTest(int _seconds)
{
    this->m_server->commandService()->lockLocalMachine();
    this->refreshActionState();
    QTimer::singleShot(_seconds * 1000, this, [this] {
        this->m_server->commandService()->unlockLocalMachine();
        this->refreshActionState();
        this->showInfo(tr("Lock test"), tr("Timed lock test finished."));
    });
}

void ServerTrayController::refreshActionState()
{
    this->m_statusAction->setText(tr("Listening on port %1").arg(this->m_server->listeningPort()));
    const bool isAdmin = PlatformIntegration::isRunningAsAdmin();
    this->m_adminAction->setEnabled(!isAdmin);
    this->m_adminAction->setText(isAdmin ? tr("Already running as administrator") : tr("Restart as administrator"));

    const bool startupEnabled = PlatformIntegration::startupEntryExists();
    this->m_startupInstallAction->setEnabled(!startupEnabled);
    this->m_startupRemoveAction->setEnabled(startupEnabled);

    const bool locked = this->m_server->commandService()->isLocked();
    this->m_lockAction->setEnabled(!locked);
    this->m_unlockAction->setEnabled(locked);
}

void ServerTrayController::showInfo(const QString& _title, const QString& _message)
{
    if (this->m_trayIcon->isVisible()) {
        this->m_trayIcon->showMessage(_title, _message, QSystemTrayIcon::Information, 3000);
    }
}

void ServerTrayController::showError(const QString& _title, const QString& _message)
{
    QMessageBox::warning(nullptr, _title, _message);
}
