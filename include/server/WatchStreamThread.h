#pragma once

#include <QByteArray>
#include <QThread>

class QTcpSocket;

/** @brief Serves one persistent remote-screen connection in a dedicated thread. */
class WatchStreamThread final : public QThread
{
public:
    /**
     * @brief Creates a monitor thread for an accepted socket.
     * @param _socket Connected socket transferred to this thread.
     * @param _parent Parent object that tracks the thread.
     */
    WatchStreamThread(QTcpSocket* _socket, QObject* _parent = nullptr);

    /** @brief Stops the stream and releases its socket. */
    ~WatchStreamThread() override;

    /** @brief Requests interruption and closes the persistent socket. */
    void stop();

protected:
    /** @brief Runs the monitor socket event loop and frame encoder. */
    void run() override;

private:
    /**
     * @brief Parses buffered frame requests and writes their responses.
     * @return true while the stream protocol remains valid; otherwise false.
     */
    [[nodiscard]] bool processAvailableRequests();

    /**
     * @brief Captures, encodes, and queues one screen frame.
     * @return true when a response is queued successfully; otherwise false.
     */
    [[nodiscard]] bool sendFrame();

    QTcpSocket* m_socket{nullptr};  ///< Persistent watch socket owned by this thread object.
    QByteArray m_buffer;            ///< Bytes waiting to form complete frame-request packets.
};
