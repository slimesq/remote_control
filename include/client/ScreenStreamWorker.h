#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

/** @brief Maintains and decodes the persistent screen stream in a worker thread. */
class ScreenStreamWorker final : public QObject
{
    Q_OBJECT

public:
    /** @brief Creates a disconnected screen-stream worker. */
    explicit ScreenStreamWorker();

    /** @brief Releases the worker-owned socket. */
    ~ScreenStreamWorker() override;

public slots:
    /**
     * @brief Sends one frame request over the persistent connection.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     * @param _generation Screen-stream generation associated with the request.
     */
    void requestFrame(QString const& _host, quint16 _port, quint64 _generation);

    /** @brief Closes the current connection while keeping the worker reusable. */
    void closeConnection();

    /** @brief Stops pending work before the worker thread exits. */
    void shutdown();

signals:
    /**
     * @brief Delivers a decoded remote-screen frame.
     * @param _generation Screen-stream generation associated with the frame.
     * @param _image Decoded remote screen image.
     */
    void frameReady(quint64 _generation, QImage const& _image);

    /**
     * @brief Reports a screen-stream connection or decoding failure.
     * @param _generation Screen-stream generation associated with the failure.
     * @param _message User-facing failure message.
     */
    void failed(quint64 _generation, QString const& _message);

    /**
     * @brief Reports that the outstanding frame request has finished.
     * @param _generation Screen-stream generation associated with the request.
     */
    void requestFinished(quint64 _generation);

private:
    /** @brief Lifecycle states of the reusable screen-stream worker. */
    enum class ScreenStreamState
    {
        Idle,          ///< No frame request is outstanding.
        FramePending,  ///< One frame request is awaiting a response.
        ShuttingDown,  ///< The worker is releasing resources before thread exit.
    };

    /** @brief Creates the thread-owned socket on first use. */
    void ensureSocket();

    /** @brief Sends the serialized frame request after connection. */
    void sendFrameRequest();

    /** @brief Sends a queued request after the socket connects. */
    void onConnected();

    /** @brief Parses and decodes available frame responses. */
    void onReadyRead();

    /** @brief Reports an unexpected disconnect for a pending request. */
    void onDisconnected();

    /**
     * @brief Reports a socket error for a pending request.
     * @param _error Socket error reported by Qt.
     */
    void onErrorOccurred(QAbstractSocket::SocketError _error);

    /** @brief Fails a frame request that produced no response before its deadline. */
    void onTimeout();

    /**
     * @brief Completes the pending request with an error.
     * @param _message User-facing failure message.
     * @param _abortConnection Whether the persistent connection must be aborted.
     */
    void failRequest(QString const& _message, bool _abortConnection);

    /**
     * @brief Closes the socket and moves the worker to a specified state.
     * @param _nextState State entered after pending work is cancelled.
     */
    void closeConnectionAndSetState(ScreenStreamState _nextState);

    /** @brief Destroys the socket and clears buffered protocol data. */
    void resetSocket();

    QTcpSocket* m_socket{nullptr};    ///< Persistent remote-screen socket.
    QTimer* m_timeoutTimer{nullptr};  ///< Deadline timer for the active frame request.
    QString m_host;                   ///< Host name or address of the active endpoint.
    quint16 m_port{0};                ///< TCP port of the active endpoint.
    quint64 m_generation{0};          ///< Screen-stream generation of the active request.
    QByteArray m_buffer;              ///< Bytes waiting to form a complete frame packet.
    ScreenStreamState m_state{ScreenStreamState::Idle};  ///< Current screen-request lifecycle.
};
