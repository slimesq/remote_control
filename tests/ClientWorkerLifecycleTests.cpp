#include "client/RemoteClient.h"

#include "common/Packet.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>
#include <functional>
#include <iostream>

namespace
{

constexpr int LifecycleIterationCount{20};
constexpr int EventProcessingSliceMs{5};
constexpr int TestTimeoutMs{3000};
constexpr qint64 StalledDownloadSize{1024};

/** @brief Provides a deterministic download response that either completes or stalls. */
class DownloadTestServer final
{
public:
    /**
     * @brief Creates a server with the requested response behavior.
     * @param _completeDownload Whether to send the complete declared file content.
     */
    explicit DownloadTestServer(bool _completeDownload) : m_completeDownload{_completeDownload}
    {
        QObject::connect(&this->m_server, &QTcpServer::newConnection, &this->m_server, [this] {
            this->acceptConnection();
        });
    }

    /**
     * @brief Starts listening on an ephemeral localhost port.
     * @return true when the listener starts successfully; otherwise false.
     */
    [[nodiscard]] bool start()
    {
        return this->m_server.listen(QHostAddress::LocalHost, 0);
    }

    /**
     * @brief Returns the ephemeral listener port.
     * @return Active listener port.
     */
    [[nodiscard]] quint16 port() const noexcept
    {
        return this->m_server.serverPort();
    }

    /**
     * @brief Returns whether a valid download request was received.
     * @return true after the request has been parsed; otherwise false.
     */
    [[nodiscard]] bool receivedRequest() const noexcept
    {
        return this->m_receivedRequest;
    }

private:
    /** @brief Accepts the next client and starts parsing its request. */
    void acceptConnection()
    {
        this->m_socket = this->m_server.nextPendingConnection();
        QObject::connect(this->m_socket, &QTcpSocket::readyRead, &this->m_server, [this] {
            this->processRequest();
        });
    }

    /** @brief Parses one download request and sends the configured response. */
    void processRequest()
    {
        this->m_requestBuffer.append(this->m_socket->readAll());
        auto const request{remote_control::Packet::tryParse(this->m_requestBuffer)};
        if (!request.has_value() || request->command != remote_control::Command::DownloadFile)
        {
            return;
        }

        this->m_receivedRequest = true;
        QByteArray sizePayload;
        QDataStream stream{&sizePayload, QIODevice::WriteOnly};
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << StalledDownloadSize;
        this->m_socket->write(
            remote_control::Packet{remote_control::Command::DownloadFile, sizePayload}.serialize());

        QByteArray const content{this->m_completeDownload
                                     ? QByteArray(static_cast<int>(StalledDownloadSize), 'x')
                                     : QByteArray(1, 'x')};
        this->m_socket->write(
            remote_control::Packet{remote_control::Command::DownloadFile, content}.serialize());
        this->m_socket->flush();
    }

    QTcpServer m_server;             ///< Local deterministic listener.
    QTcpSocket* m_socket{nullptr};   ///< Accepted socket owned by the server.
    QByteArray m_requestBuffer;      ///< Bytes waiting to form the request packet.
    bool m_completeDownload{false};  ///< Whether the response sends every declared byte.
    bool m_receivedRequest{false};   ///< Whether a download request was parsed.
};

/**
 * @brief Processes events until a condition succeeds or its deadline expires.
 * @param _condition Completion condition checked after each event slice.
 * @return true when the condition succeeds before timeout; otherwise false.
 */
bool waitUntil(std::function<bool()> const& _condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!_condition() && timer.elapsed() < TestTimeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, EventProcessingSliceMs);
        QThread::yieldCurrentThread();
    }
    return _condition();
}

/**
 * @brief Reports one failed client lifecycle expectation.
 * @param _condition Condition that must hold.
 * @param _message Failure description.
 * @return The supplied condition.
 */
bool expect(bool _condition, char const* _message)
{
    if (!_condition)
    {
        std::cerr << "FAILED: " << _message << std::endl;
    }
    return _condition;
}

/**
 * @brief Verifies cancellation completes once and removes an incomplete local file.
 * @return true when cancellation observes the current download generation.
 */
bool testDownloadCancellation()
{
    DownloadTestServer server{false};
    QTemporaryDir const temporaryDirectory;
    if (!server.start() || !temporaryDirectory.isValid())
    {
        return false;
    }

    RemoteClient client;
    int progressCount{0};
    int finishCount{0};
    bool cancellationReported{false};
    QObject::connect(&client,
                     &RemoteClient::downloadProgress,
                     &client,
                     [&progressCount](QString const&, qint64, qint64) { ++progressCount; });
    QObject::connect(&client,
                     &RemoteClient::downloadFinished,
                     &client,
                     [&finishCount, &cancellationReported](
                         QString const&, QString const&, bool _success, QString const&) {
                         ++finishCount;
                         cancellationReported = !_success;
                     });

    QString const localPath{temporaryDirectory.filePath(QStringLiteral("cancelled.bin"))};
    client.setEndpoint(QStringLiteral("127.0.0.1"), server.port());
    client.downloadRemoteFile(QStringLiteral("C:\\stalled.bin"), localPath);
    bool passed{expect(waitUntil([&server, &progressCount] {
                           return server.receivedRequest() && progressCount == 1;
                       }),
                       "the controlled download must enter its stalled transfer state")};

    client.setEndpoint(QStringLiteral("127.0.0.2"), server.port());
    passed &= expect(waitUntil([&finishCount] { return finishCount == 1; }),
                     "endpoint replacement must report one cancellation");
    passed &= expect(cancellationReported, "the cancellation result must report failure");
    passed &= expect(!QFileInfo::exists(localPath),
                     "cancellation must not commit the incomplete local file");
    return passed;
}

/**
 * @brief Verifies an old cancellation cannot finish a replacement download.
 * @return true when only the replacement operation reaches the GUI.
 */
bool testReplacementDownloadIsolation()
{
    DownloadTestServer stalledServer{false};
    DownloadTestServer completeServer{true};
    QTemporaryDir const temporaryDirectory;
    if (!stalledServer.start() || !completeServer.start() || !temporaryDirectory.isValid())
    {
        return false;
    }

    RemoteClient client;
    int finishCount{0};
    bool replacementSucceeded{false};
    QString const replacementRemotePath{QStringLiteral("C:\\replacement.bin")};
    QObject::connect(
        &client,
        &RemoteClient::downloadFinished,
        &client,
        [&finishCount, &replacementSucceeded, &replacementRemotePath](
            QString const& _remotePath, QString const&, bool _success, QString const&) {
            ++finishCount;
            replacementSucceeded = _remotePath == replacementRemotePath && _success;
        });

    client.setEndpoint(QStringLiteral("127.0.0.1"), stalledServer.port());
    client.downloadRemoteFile(QStringLiteral("C:\\old.bin"),
                              temporaryDirectory.filePath(QStringLiteral("old.bin")));
    bool passed{expect(waitUntil([&stalledServer] { return stalledServer.receivedRequest(); }),
                       "the old download must be active before endpoint replacement")};

    client.setEndpoint(QStringLiteral("127.0.0.1"), completeServer.port());
    QString const replacementPath{temporaryDirectory.filePath(QStringLiteral("replacement.bin"))};
    client.downloadRemoteFile(replacementRemotePath, replacementPath);
    passed &= expect(waitUntil([&finishCount] { return finishCount == 1; }),
                     "the replacement download must produce one visible completion");
    passed &= expect(replacementSucceeded,
                     "the old cancellation must not replace the new successful result");
    passed &= expect(QFileInfo{replacementPath}.size() == StalledDownloadSize,
                     "the replacement file must contain every declared byte");
    return passed;
}

/**
 * @brief Repeatedly destroys clients while screen and control work is queued.
 * @return true when every worker lifecycle completes without deadlock.
 */
bool testQueuedWorkShutdown()
{
    for (int iteration{0}; iteration < LifecycleIterationCount; ++iteration)
    {
        RemoteClient client;
        client.setEndpoint(QStringLiteral("127.0.0.1"), 1);
        client.requestScreenFrame();
        client.lockRemote();
    }
    return true;
}

}  // namespace

/**
 * @brief Runs client worker lifecycle regression tests.
 * @param argc Process argument count.
 * @param argv Process argument values.
 * @return EXIT_SUCCESS when worker and download lifecycles remain safe; otherwise EXIT_FAILURE.
 */
int main(int argc, char* argv[])
{
    QCoreApplication const application{argc, argv};
    bool const passed{testDownloadCancellation() && testReplacementDownloadIsolation() &&
                      testQueuedWorkShutdown()};
    std::cout << (passed ? "CLIENT WORKER LIFECYCLE TESTS PASSED"
                         : "CLIENT WORKER LIFECYCLE TESTS FAILED")
              << std::endl;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
