#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

/** @brief Maintains and decodes the persistent monitor connection in a worker thread. */
class WatchConnectionWorker final : public QObject
{
    Q_OBJECT

public:
    /** @brief Creates a disconnected monitor worker. */
    explicit WatchConnectionWorker();

    /** @brief Releases the worker-owned socket. */
    ~WatchConnectionWorker() override;

public slots:
    /**
     * @brief Sends one frame request over the persistent connection.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     * @param _generation Monitor-session generation associated with the request.
     */
    void requestFrame(QString const& _host, quint16 _port, quint64 _generation);

    /** @brief Closes the current connection while keeping the worker reusable. */
    void closeConnection();

    /** @brief Stops pending work before the worker thread exits. */
    void shutdown();

signals:
    /**
     * @brief Delivers a decoded monitor frame.
     * @param _generation Monitor-session generation associated with the frame.
     * @param _image Decoded remote screen image.
     */
    void frameReady(quint64 _generation, QImage const& _image);

    /**
     * @brief Reports a monitor connection or decoding failure.
     * @param _generation Monitor-session generation associated with the failure.
     * @param _message User-facing failure message.
     */
    void failed(quint64 _generation, QString const& _message);

    /**
     * @brief Reports that the outstanding frame request has finished.
     * @param _generation Monitor-session generation associated with the request.
     */
    void requestFinished(quint64 _generation);

private:
    /** @brief Lifecycle states of the reusable monitor worker. */
    enum class WatchState
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
    void closeConnectionAndSetState(WatchState _nextState);

    /** @brief Destroys the socket and clears buffered protocol data. */
    void resetSocket();

    QTcpSocket* m_socket{nullptr};
    QTimer* m_timeoutTimer{nullptr};
    QString m_host;
    quint16 m_port{0};
    quint64 m_generation{0};
    QByteArray m_buffer;
    WatchState m_state{WatchState::Idle};
};
