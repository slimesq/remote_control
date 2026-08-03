#pragma once

#include "common/Protocol.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>

#include <optional>

class QTcpSocket;
class QTimer;

/** @brief Maintains the ordered persistent connection used for remote input control. */
class ControlStreamWorker final : public QObject
{
    Q_OBJECT

public:
    /** @brief Creates a disconnected control worker. */
    explicit ControlStreamWorker();

    /** @brief Releases the worker-owned control socket. */
    ~ControlStreamWorker() override;

public slots:
    /**
     * @brief Queues one mouse event for ordered delivery.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     * @param _event Mouse event encoded in protocol coordinates.
     * @param _generation Control-session generation associated with the command.
     */
    void sendMouseEvent(QString const& _host,
                        quint16 _port,
                        remote_control::MouseEventPacket const& _event,
                        quint64 _generation);

    /**
     * @brief Queues a lock or unlock command for ordered delivery.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     * @param _command LockMachine or UnlockMachine.
     * @param _context User-facing command context.
     * @param _generation Control-session generation associated with the command.
     */
    void sendCommand(QString const& _host,
                     quint16 _port,
                     remote_control::Command _command,
                     QString const& _context,
                     quint64 _generation);

    /** @brief Closes the control connection and discards queued input. */
    void closeConnection();

    /** @brief Stops pending work before the control thread exits. */
    void shutdown();

signals:
    /**
     * @brief Reports a successfully completed control command.
     * @param _generation Control-session generation associated with the command.
     * @param _command Completed command.
     * @param _context Command-specific label.
     * @param _message User-facing result message.
     */
    void commandCompleted(quint64 _generation,
                          remote_control::Command _command,
                          QString const& _context,
                          QString const& _message);

    /**
     * @brief Reports a failed control command.
     * @param _generation Control-session generation associated with the command.
     * @param _command Failed command.
     * @param _context Command-specific label.
     * @param _message User-facing failure message.
     */
    void commandFailed(quint64 _generation,
                       remote_control::Command _command,
                       QString const& _context,
                       QString const& _message);

private:
    /** @brief Lifecycle states of the persistent control connection. */
    enum class ControlStreamState
    {
        Disconnected,  ///< No control connection is active.
        Connecting,    ///< The TCP connection is being established.
        Handshaking,   ///< The control-channel handshake is awaiting confirmation.
        Ready,         ///< The control channel can send queued commands.
        ShuttingDown,  ///< The worker is releasing resources before thread exit.
    };

    struct ControlCommand
    {
        remote_control::Command command{
            remote_control::Command::MouseEvent};  ///< Protocol operation to send.
        QByteArray payload;                        ///< Command-specific protocol payload.
        QString context;                           ///< Client-side label returned with the result.
        quint64 generation{0};                     ///< Control-session generation at enqueue time.
    };

    /** @brief Creates and connects the thread-owned socket on first use. */
    void ensureSocket();

    /** @brief Sends the control-channel handshake after connecting. */
    void sendHandshake();

    /** @brief Sends the next queued command when no response is outstanding. */
    void sendNextCommand();

    /**
     * @brief Adds one command while coalescing adjacent mouse moves.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     * @param _command Command and payload to enqueue.
     */
    void enqueueCommand(QString const& _host, quint16 _port, ControlCommand _command);

    /** @brief Handles completion of the TCP connection. */
    void onConnected();

    /** @brief Parses handshake and command-status responses. */
    void onReadyRead();

    /** @brief Reports an unexpected control-channel disconnect. */
    void onDisconnected();

    /**
     * @brief Reports a socket error for queued control work.
     * @param _error Socket error reported by Qt.
     */
    void onErrorOccurred(QAbstractSocket::SocketError _error);

    /** @brief Fails outstanding control work that exceeds its response deadline. */
    void onTimeout();

    /**
     * @brief Fails all outstanding commands and resets the connection.
     * @param _message User-facing failure message.
     */
    void failAllCommands(QString const& _message);

    /** @brief Destroys the control socket and clears protocol state. */
    void resetSocket();

    /**
     * @brief Returns whether a queued command represents cursor movement only.
     * @param _command Command to inspect.
     * @return true for a move-only mouse event; otherwise false.
     */
    [[nodiscard]] static bool isMouseMoveOnly(ControlCommand const& _command);

    QTcpSocket* m_socket{nullptr};    ///< Persistent socket owned by the worker thread.
    QTimer* m_timeoutTimer{nullptr};  ///< Deadline timer for connection and command responses.
    QString m_host;                   ///< Host name or address of the active endpoint.
    quint16 m_port{0};                ///< TCP port of the active endpoint.
    QByteArray m_buffer;              ///< Bytes waiting to form complete response packets.
    QQueue<ControlCommand> m_pendingCommands;         ///< Commands waiting to be sent in order.
    std::optional<ControlCommand> m_inFlightCommand;  ///< Sent command awaiting its response.
    ControlStreamState m_state{
        ControlStreamState::Disconnected};  ///< Current connection lifecycle.
};
