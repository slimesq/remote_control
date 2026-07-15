#pragma once

#include "Packet.h"

#include <QObject>

class QTcpSocket;
class QTimer;
class CommandService;

class RemoteSession : public QObject
{
    Q_OBJECT

public:
    RemoteSession(QTcpSocket* _socket, CommandService* _commandService, QObject* _parent = nullptr);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onIdleTimeout();

private:
    void processPacket(const remote_control::Packet& _packet);
    void restartIdleTimer();

    QTcpSocket* m_socket = nullptr;
    CommandService* m_commandService = nullptr;
    QTimer* m_idleTimer = nullptr;
    QByteArray m_buffer;
    bool m_handled = false;
};
