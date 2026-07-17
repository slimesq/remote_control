#pragma once

#include "common/Packet.h"

#include <QAbstractSocket>
#include <QObject>
#include <QQueue>
#include <QString>

#include <optional>

class QTcpSocket;
class QTimer;

/** @brief Maintains the ordered persistent connection used for remote input control. */
class ControlConnectionWorker final : public QObject
{
    Q_OBJECT

public:
    /** @brief Creates a disconnected control worker. */
    explicit ControlConnectionWorker();

    /** @brief Releases the worker-owned control socket. */
    ~ControlConnectionWorker() override;

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
    struct PendingCommand
    {
        remote_control::Command command{remote_control::Command::MouseEvent};
        QByteArray payload;
        QString context;
        quint64 generation{0};
    };

    /** @brief Creates and connects the thread-owned socket on first use. */
    void ensureSocket();

    /** @brief Sends the control-channel handshake after connecting. */
    void sendHandshake();

    /** @brief Sends the next queued command when no response is outstanding. */
    void sendNext();

    /**
     * @brief Adds one command while coalescing adjacent mouse moves.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     * @param _command Command and payload to enqueue.
     */
    void enqueue(QString const& _host, quint16 _port, PendingCommand _command);

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
    void failAll(QString const& _message);

    /** @brief Destroys the control socket and clears protocol state. */
    void resetSocket();

    /**
     * @brief Returns whether a queued command represents cursor movement only.
     * @param _command Command to inspect.
     * @return true for a move-only mouse event; otherwise false.
     */
    [[nodiscard]] static bool isMoveOnly(PendingCommand const& _command);

    QTcpSocket* m_socket{nullptr};
    QTimer* m_timeoutTimer{nullptr};
    QString m_host;
    quint16 m_port{0};
    QByteArray m_buffer;
    QQueue<PendingCommand> m_queue;
    std::optional<PendingCommand> m_activeCommand;
    bool m_handshakeComplete{false};
    bool m_shuttingDown{false};
};
