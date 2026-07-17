#pragma once

#include "common/Packet.h"

#include <QObject>

#include <atomic>

class QTcpSocket;

/** @brief Processes reusable one-shot file requests on a pool thread. */
class FileRequestWorker final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Creates an idle file-request worker.
     * @param _parent Parent object, or nullptr when moved to another thread.
     */
    explicit FileRequestWorker(QObject* _parent = nullptr);

    /** @brief Destroys the idle worker. */
    ~FileRequestWorker() override = default;

    /**
     * @brief Processes one accepted file-operation connection synchronously.
     * @param _socket Connected socket already assigned to this worker's thread.
     * @param _request Parsed file-operation request.
     */
    void process(QTcpSocket* _socket, remote_control::Packet const& _request);

    /** @brief Requests cancellation of the active operation. */
    void requestStop() noexcept;

signals:
    /** @brief Notifies the pool that this worker is ready for another request. */
    void requestFinished();

private:
    /**
     * @brief Enumerates a directory and streams entry packets.
     * @param _payload UTF-8 encoded directory path.
     * @return true when the complete response is written; otherwise false.
     */
    [[nodiscard]] bool streamDirectory(QByteArray const& _payload);

    /**
     * @brief Streams a file without retaining the complete file in memory.
     * @param _payload UTF-8 encoded source path.
     * @return true when the complete response is written; otherwise false.
     */
    [[nodiscard]] bool streamDownload(QByteArray const& _payload);

    /**
     * @brief Deletes a file or directory and writes its status response.
     * @param _payload UTF-8 encoded target path.
     * @return true when the response is written; otherwise false.
     */
    [[nodiscard]] bool deleteTarget(QByteArray const& _payload);

    /**
     * @brief Removes a directory tree while checking for cooperative cancellation.
     * @param _path File-system path to remove.
     * @return true when the complete tree is removed; otherwise false.
     */
    [[nodiscard]] bool removeRecursively(QString const& _path);

    /**
     * @brief Writes one packet with bounded socket buffering.
     * @param _packet Response packet to write.
     * @return true when all serialized bytes are accepted; otherwise false.
     */
    [[nodiscard]] bool writePacket(remote_control::Packet const& _packet);

    /** @brief Closes and schedules deletion of the active request socket. */
    void releaseSocket();

    std::atomic_bool m_stopping{false};
    QTcpSocket* m_socket{nullptr};
};
