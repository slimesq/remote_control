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
    RemoteSession(QTcpSocket* socket, CommandService* commandService, QObject* parent = nullptr);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onIdleTimeout();

private:
    void processPacket(const remoteqt::Packet& packet);
    void restartIdleTimer();

    QTcpSocket* m_socket = nullptr;
    CommandService* m_commandService = nullptr;
    QTimer* m_idleTimer = nullptr;
    QByteArray m_buffer;
    bool m_handled = false;
};
