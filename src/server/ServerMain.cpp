#include "CommandService.h"
#include "PlatformIntegration.h"
#include "RemoteServer.h"
#include "ServerTrayController.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Remote Control Qt Server"));
    parser.addHelpOption();

    QCommandLineOption portOption({ QStringLiteral("p"), QStringLiteral("port") }, QStringLiteral("Listen port"), QStringLiteral("port"), QStringLiteral("9527"));
    QCommandLineOption installStartupOption(QStringLiteral("install-startup"), QStringLiteral("Install the current server into the current user's startup items."));
    QCommandLineOption removeStartupOption(QStringLiteral("remove-startup"), QStringLiteral("Remove the current server from the current user's startup items."));
    QCommandLineOption elevateOption(QStringLiteral("elevate"), QStringLiteral("Restart the server with administrator privileges and exit."));
    QCommandLineOption noTrayOption(QStringLiteral("no-tray"), QStringLiteral("Run without creating a tray icon."));
    QCommandLineOption lockTestOption(QStringLiteral("lock-test"), QStringLiteral("Run a timed lock test in seconds."), QStringLiteral("seconds"));
    parser.addOption(portOption);
    parser.addOption(installStartupOption);
    parser.addOption(removeStartupOption);
    parser.addOption(elevateOption);
    parser.addOption(noTrayOption);
    parser.addOption(lockTestOption);
    parser.process(app);

    if (parser.isSet(elevateOption)) {
        QString errorMessage;
        if (!PlatformIntegration::relaunchElevated({}, &errorMessage)) {
            QMessageBox::critical(nullptr, QObject::tr("Elevation failed"), errorMessage);
            return 1;
        }
        return 0;
    }

    if (parser.isSet(installStartupOption)) {
        QString errorMessage;
        if (!PlatformIntegration::installStartupEntry(&errorMessage)) {
            QMessageBox::critical(nullptr, QObject::tr("Startup"), errorMessage);
            return 1;
        }
        return 0;
    }

    if (parser.isSet(removeStartupOption)) {
        QString errorMessage;
        if (!PlatformIntegration::removeStartupEntry(&errorMessage)) {
            QMessageBox::critical(nullptr, QObject::tr("Startup"), errorMessage);
            return 1;
        }
        return 0;
    }

    const quint16 port = parser.value(portOption).toUShort();

    RemoteServer server;
    if (!server.start(port)) {
        QMessageBox::critical(nullptr, QObject::tr("Startup failed"), QObject::tr("Could not listen on port %1.").arg(port));
        return 1;
    }

    ServerTrayController* trayController = nullptr;
    if (!parser.isSet(noTrayOption) && QSystemTrayIcon::isSystemTrayAvailable()) {
        trayController = new ServerTrayController(&server, &app);
        trayController->show();
    }

    if (parser.isSet(lockTestOption)) {
        const int seconds = qMax(1, parser.value(lockTestOption).toInt());
        if (trayController) {
            trayController->runTimedLockTest(seconds);
        } else {
            server.commandService()->lockLocalMachine();
            QTimer::singleShot(seconds * 1000, &app, [&server] {
                server.commandService()->unlockLocalMachine();
            });
        }
    }

    return app.exec();
}
