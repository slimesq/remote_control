#include "server/CommandService.h"
#include "server/PlatformIntegration.h"
#include "server/RemoteServer.h"
#include "server/ServerTrayController.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QStringList>
#include <QSystemTrayIcon>

#include <cstdlib>

/**
 * @brief Starts the remote-control server or performs a maintenance action.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument array.
 * @return Process exit code.
 */
int main(int argc, char* argv[])
{
    QApplication app{argc, argv};
    app.setQuitOnLastWindowClosed(false);

    // 1. Parse runtime options and one-shot maintenance actions.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Remote Control Qt Server"));
    parser.addHelpOption();

    // -p/--port: Sets the server listen port; defaults to 9527 when omitted.
    QCommandLineOption const portOption{QStringList{QStringLiteral("p"), QStringLiteral("port")},
                                        QStringLiteral("Listen port"),
                                        QStringLiteral("port"),
                                        QStringLiteral("9527")};
    // --install-startup: Adds the startup entry without starting the server.
    QCommandLineOption const installStartupOption{
        QStringLiteral("install-startup"),
        QStringLiteral("Add the startup entry without starting the server.")};
    // --remove-startup: Removes the startup entry without starting the server.
    QCommandLineOption const removeStartupOption{
        QStringLiteral("remove-startup"),
        QStringLiteral("Remove the startup entry without starting the server.")};
    // --elevate: Starts a new server process with administrator privileges.
    QCommandLineOption const elevateOption{QStringLiteral("elevate"),
                                           QStringLiteral("Restart the server as administrator.")};
    // --no-tray: Disables the system tray icon while keeping the server running.
    QCommandLineOption const noTrayOption{QStringLiteral("no-tray"),
                                          QStringLiteral("Run without creating a tray icon.")};
    // --lock-test <seconds>: Locks the screen for the specified duration, then
    // unlocks it automatically.
    QCommandLineOption const lockTestOption{QStringLiteral("lock-test"),
                                            QStringLiteral("Run a timed lock test in seconds."),
                                            QStringLiteral("seconds")};
    parser.addOption(portOption);
    parser.addOption(installStartupOption);
    parser.addOption(removeStartupOption);
    parser.addOption(elevateOption);
    parser.addOption(noTrayOption);
    parser.addOption(lockTestOption);
    parser.process(app);

    // 2. Complete one-shot actions before creating a listening server.
    if (parser.isSet(elevateOption))
    {
        QStringList elevatedArguments{QCoreApplication::arguments()};
        elevatedArguments.removeFirst();                           // Remove the executable path.
        elevatedArguments.removeAll(QStringLiteral("--elevate"));  // Prevent recursive elevation.

        QString errorMessage;
        if (!PlatformIntegration::relaunchElevated(elevatedArguments, &errorMessage))
        {
            QMessageBox::critical(nullptr, QObject::tr("Elevation failed"), errorMessage);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (parser.isSet(installStartupOption))
    {
        QString errorMessage;
        if (!PlatformIntegration::installStartupEntry(&errorMessage))
        {
            QMessageBox::critical(nullptr, QObject::tr("Startup"), errorMessage);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (parser.isSet(removeStartupOption))
    {
        QString errorMessage;
        if (!PlatformIntegration::removeStartupEntry(&errorMessage))
        {
            QMessageBox::critical(nullptr, QObject::tr("Startup"), errorMessage);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    // 3. Start listening before enabling optional local UI features.
    quint16 const port{parser.value(portOption).toUShort()};

    RemoteServer server;
    if (!server.start(port))
    {
        QMessageBox::critical(nullptr,
                              QObject::tr("Startup failed"),
                              QObject::tr("Could not listen on port %1.").arg(port));
        return EXIT_FAILURE;
    }

    // 4. Enable optional tray and timed-lock features only for a running server.
    if (!parser.isSet(noTrayOption) && QSystemTrayIcon::isSystemTrayAvailable())
    {
        auto* const trayController{new ServerTrayController{&server, &app}};
        trayController->show();
    }

    if (parser.isSet(lockTestOption))
    {
        int const seconds{qMax(1, parser.value(lockTestOption).toInt())};
        server.commandService()->runTimedLockTest(seconds);
    }

    return app.exec();
}
