#pragma once

#include "common/Packet.h"

#include <QObject>

class QTcpSocket;
class QTimer;
class CommandService;

/** @brief Parses requests and writes responses for one connected TCP client. */
class RemoteSession : public QObject
{
    Q_OBJECT

public:
    /** @brief Creates a session for an accepted socket and shared command service. */
    RemoteSession(QTcpSocket* _socket, CommandService* _commandService, QObject* _parent = nullptr);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onIdleTimeout();

private:
    void processPacket(remote_control::Packet const& _packet);
    void restartIdleTimer();

    QTcpSocket* m_socket{nullptr};
    CommandService* m_commandService{nullptr};
    QTimer* m_idleTimer{nullptr};
    QByteArray m_buffer;
    bool m_handled{false};
};
