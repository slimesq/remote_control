#pragma once

#include "../common/Packet.h"

#include <QObject>

class QTcpSocket;
class CommandService;

class RemoteSession : public QObject
{
    Q_OBJECT

public:
    RemoteSession(QTcpSocket* socket, CommandService* commandService, QObject* parent = nullptr);

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void processPacket(const remoteqt::Packet& packet);

    QTcpSocket* m_socket = nullptr;
    CommandService* m_commandService = nullptr;
    QByteArray m_buffer;
    bool m_handled = false;
};
