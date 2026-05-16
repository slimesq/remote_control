#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QTcpSocket>
#include <QThread>

namespace {

constexpr quint16 DefaultServerPort = 9527;
constexpr int ProbeTimeoutMs = 250;
constexpr int StartupTimeoutMs = 8000;

bool isServerListening(const QString& host, quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(host, port);
    const bool connected = socket.waitForConnected(ProbeTimeoutMs);
    if (connected) {
        socket.disconnectFromHost();
    }
    return connected;
}

bool ensureLocalServerRunning(const QString& host, quint16 port, bool noTray, QString* errorMessage)
{
    if (isServerListening(host, port)) {
        return true;
    }

    const QString serverPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("remote_server_qt.exe"));
    if (!QFileInfo::exists(serverPath)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("The server executable was not found: %1").arg(serverPath);
        }
        return false;
    }

    QStringList arguments { QStringLiteral("--port"), QString::number(port) };
    if (noTray) {
        arguments.push_back(QStringLiteral("--no-tray"));
    }

    if (!QProcess::startDetached(serverPath, arguments, QFileInfo(serverPath).absolutePath())) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Failed to start the local server.");
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < StartupTimeoutMs) {
        if (isServerListening(host, port)) {
            return true;
        }
        QThread::msleep(150);
    }

    if (errorMessage) {
        *errorMessage = QObject::tr("The local server did not start listening on %1:%2 in time.")
                            .arg(host)
                            .arg(port);
    }
    return false;
}

}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Remote Control Qt Client"));
    parser.addHelpOption();

    QCommandLineOption noLocalServerOption(QStringLiteral("no-local-server"), QStringLiteral("Do not auto-start the local server."));
    QCommandLineOption showServerTrayOption(QStringLiteral("server-tray"), QStringLiteral("Start the local server with its tray icon."));
    QCommandLineOption serverHostOption(QStringLiteral("server-host"), QStringLiteral("Local server host"), QStringLiteral("host"), QStringLiteral("127.0.0.1"));
    QCommandLineOption serverPortOption(QStringLiteral("server-port"), QStringLiteral("Local server port"), QStringLiteral("port"), QString::number(DefaultServerPort));
    parser.addOption(noLocalServerOption);
    parser.addOption(showServerTrayOption);
    parser.addOption(serverHostOption);
    parser.addOption(serverPortOption);
    parser.process(app);

    if (!parser.isSet(noLocalServerOption)) {
        const QString serverHost = parser.value(serverHostOption).trimmed();
        const quint16 serverPort = parser.value(serverPortOption).toUShort();
        QString errorMessage;
        if (!ensureLocalServerRunning(serverHost.isEmpty() ? QStringLiteral("127.0.0.1") : serverHost,
                serverPort == 0 ? DefaultServerPort : serverPort,
                !parser.isSet(showServerTrayOption),
                &errorMessage)) {
            QMessageBox::warning(nullptr, QObject::tr("Server Startup"), errorMessage);
        }
    }

    MainWindow window;
    window.show();
    return app.exec();
}
