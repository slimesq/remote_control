#pragma once

#include <QObject>

class QTcpServer;
class CommandService;

class RemoteServer : public QObject
{
    Q_OBJECT

public:
    explicit RemoteServer(QObject* parent = nullptr);

    bool start(quint16 port = 9527);
    quint16 listeningPort() const;
    CommandService* commandService() const;

private slots:
    void onNewConnection();

private:
    QTcpServer* m_server = nullptr;
    CommandService* m_commandService = nullptr;
};
