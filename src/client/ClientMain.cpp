#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QTcpSocket>

namespace {

constexpr quint16 DefaultServerPort = 9527;
constexpr int ProbeTimeoutMs = 250;

/**
 * @brief Ensures that a local remote server is available.
 *
 * The function first probes the specified host and port. If a server is already
 * listening, it returns immediately. Otherwise, it tries to start
 * RemoteControlServer.exe from the client executable directory.
 *
 * @param _host Host name or IP address to probe.
 * @param _port TCP port used by the local server.
 * @param _noTray When true, starts the server with --no-tray so no system tray
 * icon is shown.
 * @param _errorMessage Optional output parameter that receives a user-facing
 * error message when startup fails.
 * @return true if an existing server was found or a new server was started;
 * false otherwise.
 */
bool tryStartLocalServer(const QString &_host, quint16 _port, bool _noTray,
                         QString *_errorMessage) {
    // Prefer reusing an already-running local server so toolbar runs stay cheap.
    QTcpSocket probeSocket;
    probeSocket.connectToHost(_host, _port);
    if (probeSocket.waitForConnected(ProbeTimeoutMs)) {
        probeSocket.disconnectFromHost();
        return true;
    }

    const QString serverPath =
        QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("RemoteControlServer.exe"));
    if (!QFileInfo::exists(serverPath)) {
        if (_errorMessage) {
            *_errorMessage = QObject::tr("The server executable was not found: %1")
                                .arg(serverPath);
        }
        return false;
    }

    QStringList arguments{QStringLiteral("--port"), QString::number(_port)};
    if (_noTray) {
        arguments.push_back(QStringLiteral("--no-tray"));
    }

    if (!QProcess::startDetached(serverPath, arguments,
                                 QFileInfo(serverPath).absolutePath())) {
        if (_errorMessage) {
            *_errorMessage = QObject::tr("Failed to start the local server.");
        }
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Remote Control Qt Client"));
    parser.addHelpOption();

    QCommandLineOption noLocalServerOption(
        QStringLiteral("no-local-server"),
        QStringLiteral("Do not auto-start the local server."));
    QCommandLineOption showServerTrayOption(
        QStringLiteral("server-tray"),
        QStringLiteral("Start the local server with its tray icon."));
    QCommandLineOption serverHostOption(
        QStringLiteral("server-host"), QStringLiteral("Local server host"),
        QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption serverPortOption(
        QStringLiteral("server-port"), QStringLiteral("Local server port"),
        QStringLiteral("port"), QString::number(DefaultServerPort));
    parser.addOption(noLocalServerOption);
    parser.addOption(showServerTrayOption);
    parser.addOption(serverHostOption);
    parser.addOption(serverPortOption);
    parser.process(app);

    const QString resolvedServerHost =
        parser.value(serverHostOption).trimmed().isEmpty()
                                           ? QStringLiteral("127.0.0.1")
                                           : parser.value(serverHostOption).trimmed();
    const quint16 resolvedServerPort =
        parser.value(serverPortOption).toUShort() == 0
                                           ? DefaultServerPort
                                           : parser.value(serverPortOption).toUShort();

    if (!parser.isSet(noLocalServerOption)) {
        QString errorMessage;
        if (!tryStartLocalServer(resolvedServerHost, resolvedServerPort,
                                 !parser.isSet(showServerTrayOption),
                                 &errorMessage)) {
            QMessageBox::warning(nullptr, QObject::tr("Server Startup"),
                                 errorMessage);
        }
    }

    // Keep the auto-start endpoint and the UI endpoint aligned for non-default
    // host/port runs.
    MainWindow window;
    window.setEndpoint(resolvedServerHost, resolvedServerPort);
    window.show();
    return app.exec();
}
