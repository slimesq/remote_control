#pragma once

#include "common/Packet.h"

#include <QByteArray>
#include <QThread>

class CommandService;
class QTcpSocket;

/** @brief Serves ordered input and lock commands over one persistent control connection. */
class ControlStreamThread final : public QThread
{
public:
    /**
     * @brief Creates a control-channel thread for an accepted socket.
     * @param _socket Connected socket transferred to this thread.
     * @param _commandService GUI-thread service used for lock-window operations.
     * @param _parent Parent object that tracks the thread.
     */
    ControlStreamThread(QTcpSocket* _socket,
                        CommandService* _commandService,
                        QObject* _parent = nullptr);

    /** @brief Stops the control channel and releases its socket. */
    ~ControlStreamThread() override;

    /** @brief Requests interruption and closes the persistent socket. */
    void stop();

protected:
    /** @brief Runs the control socket event loop in the dedicated thread. */
    void run() override;

private:
    /**
     * @brief Parses and executes all complete buffered control requests.
     * @return true while the control protocol remains valid; otherwise false.
     */
    [[nodiscard]] bool processAvailableRequests();

    /**
     * @brief Executes one supported control request.
     * @param _request Parsed mouse, lock, or unlock request.
     * @return Command-status response packet.
     */
    [[nodiscard]] remote_control::Packet handleRequest(
        remote_control::Packet const& _request) const;

    /**
     * @brief Queues a lock-window operation on the server GUI thread.
     * @param _lock true to lock; false to unlock.
     * @return true when the operation is queued successfully; otherwise false.
     */
    [[nodiscard]] bool invokeLockOperation(bool _lock) const;

    QTcpSocket* m_socket{nullptr};
    CommandService* m_commandService{nullptr};
    QByteArray m_buffer;
};
