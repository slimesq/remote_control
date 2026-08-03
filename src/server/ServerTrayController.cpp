#include "server/ServerTrayController.h"

#include "server/ScreenLockService.h"
#include "server/WindowsPlatformIntegration.h"
#include "server/RemoteControlServer.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QSystemTrayIcon>

namespace
{

constexpr int LockTestDurationSeconds{5};
constexpr int TrayMessageDurationMs{3000};

/**
 * @brief Preserves current server options and adds a safe elevation handover marker.
 * @return Arguments for the elevated replacement process.
 */
QStringList elevatedRelaunchArguments()
{
    QStringList arguments{QCoreApplication::arguments()};
    arguments.removeFirst();
    arguments.removeAll(QStringLiteral("--elevate"));

    // Remove a stale handover marker before adding the current server process identifier.
    for (int index{0}; index < arguments.size();)
    {
        QString const& argument{arguments[index]};
        if (argument == QStringLiteral("--wait-for-pid"))
        {
            arguments.removeAt(index);
            if (index < arguments.size())
            {
                arguments.removeAt(index);
            }
        }
        else if (argument.startsWith(QStringLiteral("--wait-for-pid=")))
        {
            arguments.removeAt(index);
        }
        else
        {
            ++index;
        }
    }

    arguments.append(QStringLiteral("--wait-for-pid"));
    arguments.append(QString::number(QCoreApplication::applicationPid()));
    return arguments;
}

}  // namespace

ServerTrayController::ServerTrayController(RemoteControlServer* _server, QObject* _parent)
    : QObject{_parent},
      m_server{_server},
      m_trayIcon{new QSystemTrayIcon{this}},
      m_trayMenu{new QMenu{}}
{
    this->m_trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    connect(this, &QObject::destroyed, this->m_trayMenu, &QObject::deleteLater);

    ScreenLockService* const screenLockService{this->m_server->screenLockService()};
    connect(screenLockService, &ScreenLockService::lockStateChanged, this, [this] {
        this->refreshActionState();
    });
    connect(screenLockService, &ScreenLockService::timedLockTestFinished, this, [this] {
        this->showInfo(tr("Lock test"), tr("Timed lock test finished."));
    });

    this->m_statusAction = this->m_trayMenu->addAction(QString());
    this->m_statusAction->setEnabled(false);

    this->m_elevateAction = this->m_trayMenu->addAction(tr("Restart as administrator"));
    connect(this->m_elevateAction, &QAction::triggered, this, [this] {
        QString errorMessage;
        if (!WindowsPlatformIntegration::relaunchElevated(elevatedRelaunchArguments(),
                                                          &errorMessage))
        {
            this->showError(tr("Elevation failed"), errorMessage);
            return;
        }
        QCoreApplication::quit();
    });

    this->m_trayMenu->addSeparator();

    this->m_installStartupAction = this->m_trayMenu->addAction(tr("Enable startup"));
    connect(this->m_installStartupAction, &QAction::triggered, this, [this] {
        QString errorMessage;
        if (!WindowsPlatformIntegration::installStartupEntry(&errorMessage))
        {
            this->showError(tr("Startup"), errorMessage);
            return;
        }
        this->refreshActionState();
        this->showInfo(tr("Startup"), tr("Startup has been enabled."));
    });

    this->m_removeStartupAction = this->m_trayMenu->addAction(tr("Disable startup"));
    connect(this->m_removeStartupAction, &QAction::triggered, this, [this] {
        QString errorMessage;
        if (!WindowsPlatformIntegration::removeStartupEntry(&errorMessage))
        {
            this->showError(tr("Startup"), errorMessage);
            return;
        }
        this->refreshActionState();
        this->showInfo(tr("Startup"), tr("Startup has been disabled."));
    });

    this->m_trayMenu->addSeparator();

    this->m_lockAction = this->m_trayMenu->addAction(tr("Lock machine"));
    connect(this->m_lockAction, &QAction::triggered, this, [this] {
        this->m_server->screenLockService()->lockScreen();
    });

    this->m_unlockAction = this->m_trayMenu->addAction(tr("Unlock machine"));
    connect(this->m_unlockAction, &QAction::triggered, this, [this] {
        this->m_server->screenLockService()->unlockScreen();
    });

    this->m_lockTestAction =
        this->m_trayMenu->addAction(tr("Run %1s lock test").arg(LockTestDurationSeconds));
    connect(this->m_lockTestAction, &QAction::triggered, this, [this] {
        this->m_server->screenLockService()->runTimedLockTest(LockTestDurationSeconds);
    });

    this->m_trayMenu->addSeparator();

    this->m_quitAction = this->m_trayMenu->addAction(tr("Exit"));
    connect(this->m_quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    this->m_trayIcon->setContextMenu(this->m_trayMenu);
    connect(this->m_trayIcon,
            &QSystemTrayIcon::activated,
            this,
            [this](QSystemTrayIcon::ActivationReason _reason) {
                // Show status for normal activation gestures, not context-menu or unknown events.
                if (_reason == QSystemTrayIcon::Trigger || _reason == QSystemTrayIcon::DoubleClick)
                {
                    this->showInfo(
                        tr("Remote Server"),
                        tr("Server is listening on port %1.").arg(this->m_server->listeningPort()));
                }
            });

    this->refreshActionState();
}

void ServerTrayController::show()
{
    this->m_trayIcon->show();
    this->showInfo(tr("Remote Server"),
                   tr("Server is listening on port %1.").arg(this->m_server->listeningPort()));
}

void ServerTrayController::refreshActionState()
{
    this->m_statusAction->setText(tr("Listening on port %1").arg(this->m_server->listeningPort()));
    bool const isElevated{WindowsPlatformIntegration::isRunningAsAdmin()};
    this->m_elevateAction->setEnabled(!isElevated);
    this->m_elevateAction->setText(isElevated ? tr("Already running as administrator")
                                              : tr("Restart as administrator"));

    bool const startupEnabled{WindowsPlatformIntegration::startupEntryExists()};
    this->m_installStartupAction->setEnabled(!startupEnabled);
    this->m_removeStartupAction->setEnabled(startupEnabled);

    bool const locked{this->m_server->screenLockService()->isScreenLocked()};
    this->m_lockAction->setEnabled(!locked);
    this->m_unlockAction->setEnabled(locked);
}

void ServerTrayController::showInfo(QString const& _title, QString const& _message)
{
    if (this->m_trayIcon->isVisible())
    {
        this->m_trayIcon->showMessage(
            _title, _message, QSystemTrayIcon::Information, TrayMessageDurationMs);
    }
}

void ServerTrayController::showError(QString const& _title, QString const& _message)
{
    QMessageBox::warning(nullptr, _title, _message);
}
