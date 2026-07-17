#pragma once

#include "common/Protocol.h"

#include <QObject>

class QTcpServer;
class CommandService;

/** @brief Accepts TCP clients and creates an isolated session for each connection. */
class RemoteServer : public QObject
{
    Q_OBJECT

public:
    /** @brief Creates a stopped remote-control server. */
    explicit RemoteServer(QObject* _parent = nullptr);

    /** @brief Starts listening on the requested TCP port. */
    [[nodiscard]] bool start(quint16 _port = remote_control::DefaultServerPort);

    /** @brief Returns the TCP port currently used by the server. */
    [[nodiscard]] quint16 listeningPort() const noexcept;

    /** @brief Returns the command service shared by active sessions. */
    [[nodiscard]] CommandService* commandService() const noexcept;

private slots:
    void onNewConnection();

private:
    QTcpServer* m_server{nullptr};
    CommandService* m_commandService{nullptr};
};
