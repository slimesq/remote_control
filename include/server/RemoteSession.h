#pragma once

#include "common/Packet.h"

#include <QObject>

class QTcpSocket;
class QTimer;
class CommandService;
class RemoteServer;

/** @brief Parses requests and writes responses for one connected TCP client. */
class RemoteSession : public QObject
{
public:
    /**
     * @brief Creates a session for an accepted socket and shared command service.
     * @param _socket Accepted client socket transferred to this session's ownership.
     * @param _server Server that owns command handling and monitor threads.
     * @param _parent Parent object, or nullptr.
     */
    RemoteSession(QTcpSocket* _socket, RemoteServer* _server, QObject* _parent = nullptr);

private:
    /** @brief Parses newly received request data. */
    void onReadyRead();

    /** @brief Schedules cleanup after socket disconnection. */
    void onDisconnected();

    /** @brief Closes an idle client connection. */
    void onIdleTimeout();

    /**
     * @brief Executes a request packet and writes its responses.
     * @param _packet Parsed request packet.
     */
    void processPacket(remote_control::Packet const& _packet);

    /**
     * @brief Detaches the accepted socket from this short-lived session.
     * @return Connected socket ready to move to a worker thread.
     */
    [[nodiscard]] QTcpSocket* takeSocket();

    /** @brief Restarts the connection idle timeout. */
    void restartIdleTimer();

    QTcpSocket* m_socket{nullptr};    ///< Accepted socket until ownership is transferred.
    RemoteServer* m_server{nullptr};  ///< Non-owning server used to route persistent sessions.
    CommandService* m_commandService{nullptr};  ///< Non-owning short-command service.
    QTimer* m_idleTimer{nullptr};  ///< Rejects sessions that do not provide a complete request.
    QByteArray m_buffer;           ///< Bytes waiting to form the initial request packet.
    bool m_handled{false};         ///< Whether the initial request has been dispatched.
};
