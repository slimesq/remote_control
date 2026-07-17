#pragma once

#include "common/Packet.h"

#include <QList>
#include <QObject>
#include <QQueue>

class FileRequestWorker;
class QTcpSocket;
class QThread;

/** @brief Reuses a bounded set of threads for one-shot file operations. */
class FileRequestPool final : public QObject
{
public:
    /**
     * @brief Creates an initially empty, lazily grown worker pool.
     * @param _maxThreadCount Maximum number of concurrent file operations.
     * @param _parent Parent object that owns the pool.
     */
    explicit FileRequestPool(int _maxThreadCount, QObject* _parent = nullptr);

    /** @brief Stops active workers and discards queued requests. */
    ~FileRequestPool() override;

    /**
     * @brief Queues a file request or dispatches it to an idle worker.
     * @param _socket Connected socket owned by the calling thread.
     * @param _request Parsed file-operation request.
     * @return true when the request is accepted; otherwise false.
     */
    [[nodiscard]] bool submit(QTcpSocket* _socket, remote_control::Packet _request);

    /** @brief Cancels queued work, stops active workers, and joins all pool threads. */
    void shutdown();

private:
    struct PendingRequest
    {
        QTcpSocket* socket{nullptr};
        remote_control::Packet request;
    };

    /**
     * @brief Creates and starts one reusable worker thread.
     * @return Newly created idle worker.
     */
    [[nodiscard]] FileRequestWorker* createWorker();

    /**
     * @brief Transfers and queues one request on a selected worker.
     * @param _worker Idle worker that will process the request.
     * @param _pending Socket and packet to transfer.
     */
    void dispatch(FileRequestWorker* _worker, PendingRequest _pending);

    /**
     * @brief Reuses a completed worker or returns it to the idle list.
     * @param _worker Worker that completed its previous request.
     */
    void onRequestFinished(FileRequestWorker* _worker);

    int m_maxThreadCount{1};
    bool m_stopping{false};
    QList<QThread*> m_threads;
    QList<FileRequestWorker*> m_workers;
    QList<FileRequestWorker*> m_idleWorkers;
    QQueue<PendingRequest> m_pendingRequests;
};
