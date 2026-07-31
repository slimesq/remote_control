#include "server/FileRequestPool.h"

#include "server/FileRequestWorker.h"

#include <QMetaObject>
#include <QTcpSocket>
#include <QThread>

#include <utility>

namespace
{

constexpr int MaxQueuedFileRequests{64};

}  // namespace

FileRequestPool::FileRequestPool(int _maxThreadCount, QObject* _parent)
    : QObject{_parent}, m_maxThreadCount{qMax(1, _maxThreadCount)}
{
}

FileRequestPool::~FileRequestPool()
{
    this->shutdown();
}

bool FileRequestPool::submit(QTcpSocket* _socket, remote_control::Packet _request)
{
    // Reject work when the pool cannot own it safely: shutdown has begun, no socket was supplied,
    // or the bounded backlog is full.
    if (this->m_stopping || !_socket || this->m_pendingRequests.size() >= MaxQueuedFileRequests)
    {
        return false;
    }

    PendingRequest pending{_socket, std::move(_request)};
    // Prefer an existing idle worker, then grow to the configured limit, then queue the request.
    if (!this->m_idleWorkers.isEmpty())
    {
        this->dispatch(this->m_idleWorkers.takeLast(), std::move(pending));
        return true;
    }

    if (this->m_workers.size() < this->m_maxThreadCount)
    {
        this->dispatch(this->createWorker(), std::move(pending));
        return true;
    }

    this->m_pendingRequests.enqueue(pending);
    return true;
}

void FileRequestPool::shutdown()
{
    if (this->m_stopping)
    {
        return;
    }
    this->m_stopping = true;

    // 1. Reject work that has not yet acquired a pool thread.
    while (!this->m_pendingRequests.isEmpty())
    {
        PendingRequest const pending{this->m_pendingRequests.dequeue()};
        pending.socket->abort();
        pending.socket->deleteLater();
    }

    // 2. Cooperatively cancel active I/O before stopping the reusable event loops.
    for (FileRequestWorker* const worker : this->m_workers)
    {
        worker->requestStop();
    }
    for (QThread* const thread : this->m_threads)
    {
        thread->quit();
    }
    for (QThread* const thread : this->m_threads)
    {
        thread->wait();
    }

    this->m_idleWorkers.clear();
    this->m_workers.clear();
}

FileRequestWorker* FileRequestPool::createWorker()
{
    auto* const thread{new QThread{this}};
    auto* const worker{new FileRequestWorker{}};
    worker->moveToThread(thread);
    QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    QObject::connect(worker, &FileRequestWorker::requestFinished, this, [this, worker] {
        this->onRequestFinished(worker);
    });
    this->m_threads.append(thread);
    this->m_workers.append(worker);
    thread->start();
    return worker;
}

void FileRequestPool::dispatch(FileRequestWorker* _worker, PendingRequest _pending)
{
    _pending.socket->moveToThread(_worker->thread());
    QMetaObject::invokeMethod(
        _worker,
        [_worker, pending = std::move(_pending)] {
            _worker->process(pending.socket, pending.request);
        },
        Qt::QueuedConnection);
}

void FileRequestPool::onRequestFinished(FileRequestWorker* _worker)
{
    // A worker completing during shutdown must not be reused or returned to the idle collection.
    if (this->m_stopping)
    {
        return;
    }
    if (!this->m_pendingRequests.isEmpty())
    {
        this->dispatch(_worker, this->m_pendingRequests.dequeue());
        return;
    }
    this->m_idleWorkers.append(_worker);
}
