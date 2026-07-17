#include "client/MainWindow.h"
#include "common/Protocol.h"

#include <QApplication>
#include <QCommandLineParser>

/**
 * @brief Starts the remote-control client application.
 * @param _argc Number of command-line arguments.
 * @param _argv Command-line argument array.
 * @return Process exit code.
 */
int main(int _argc, char* _argv[])
{
    QApplication const app{_argc, _argv};

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Remote Control Qt Client"));
    parser.addHelpOption();

    QCommandLineOption const serverHostOption{QStringLiteral("server-host"),
                                              QStringLiteral("Remote server host"),
                                              QStringLiteral("host"),
                                              QStringLiteral("127.0.0.1")};
    QCommandLineOption const serverPortOption{QStringLiteral("server-port"),
                                              QStringLiteral("Remote server port"),
                                              QStringLiteral("port"),
                                              QString::number(remote_control::DefaultServerPort)};
    parser.addOption(serverHostOption);
    parser.addOption(serverPortOption);
    parser.process(app);

    QString const requestedServerHost{parser.value(serverHostOption).trimmed()};
    QString const resolvedServerHost{requestedServerHost.isEmpty() ? QStringLiteral("127.0.0.1")
                                                                   : requestedServerHost};

    bool portIsValid{false};
    quint16 const requestedServerPort{parser.value(serverPortOption).toUShort(&portIsValid)};
    quint16 const resolvedServerPort{portIsValid && requestedServerPort != 0
                                         ? requestedServerPort
                                         : remote_control::DefaultServerPort};

    MainWindow window;
    window.setEndpoint(resolvedServerHost, resolvedServerPort);
    window.show();
    return app.exec();
}
