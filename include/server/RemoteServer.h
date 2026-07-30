#pragma once

#include "common/Protocol.h"

#include <QList>
#include <QObject>

class QTcpServer;
class QTcpSocket;
class CommandService;
class ControlStreamThread;
class FileRequestPool;
class RemoteSession;
class WatchStreamThread;

namespace remote_control
{
class Packet;
}

/** @brief Accepts TCP clients and creates an isolated session for each connection. */
class RemoteServer : public QObject
{
public:
    /**
     * @brief Creates a stopped remote-control server.
     * @param _parent Parent object, or nullptr.
     */
    explicit RemoteServer(QObject* _parent = nullptr);

    /** @brief Stops active worker threads before server-owned services are destroyed. */
    ~RemoteServer() override
    {
        this->shutdownWorkers();
    }

    /**
     * @brief Starts listening on the requested TCP port.
     * @param _port TCP port to listen on.
     * @return true when listening starts successfully; otherwise false.
     */
    [[nodiscard]] bool start(quint16 _port = remote_control::DefaultServerPort);

    /**
     * @brief Returns the TCP port currently used by the server.
     * @return Active listening port, or zero when not listening.
     */
    [[nodiscard]] quint16 listeningPort() const noexcept;

    /**
     * @brief Returns the command service shared by active sessions.
     * @return Parent-owned command service.
     */
    [[nodiscard]] CommandService* commandService() const noexcept;

private:
    friend class RemoteSession;

    /** @brief Creates sessions for all pending client connections. */
    void onNewConnection();

    /**
     * @brief Transfers a monitor socket to a dedicated persistent-stream thread.
     * @param _socket Connected monitor socket without a QObject parent.
     */
    void startWatchStream(QTcpSocket* _socket);

    /**
     * @brief Transfers a control socket to a dedicated persistent-stream thread.
     * @param _socket Connected control socket without a QObject parent.
     */
    void startControlStream(QTcpSocket* _socket);

    /**
     * @brief Transfers a file request and socket to the reusable file-worker pool.
     * @param _socket Connected request socket without a QObject parent.
     * @param _request Parsed file-operation request.
     */
    void startFileRequest(QTcpSocket* _socket, remote_control::Packet _request);

    /** @brief Stops and joins all active server worker threads. */
    void shutdownWorkers();

    QTcpServer* m_server{nullptr};                 ///< Parent-owned TCP listener.
    CommandService* m_commandService{nullptr};     ///< Parent-owned command execution service.
    FileRequestPool* m_fileRequestPool{nullptr};   ///< Parent-owned reusable file-worker pool.
    QList<WatchStreamThread*> m_watchThreads;      ///< Active remote-screen stream threads.
    QList<ControlStreamThread*> m_controlThreads;  ///< Active remote-control stream threads.
};
