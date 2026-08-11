#include "client/RemoteClient.h"

#include "client/ControlStreamWorker.h"
#include "client/FileDownloadWorker.h"
#include "client/ScreenStreamWorker.h"
#include "common/Packet.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <algorithm>

namespace
{

constexpr int RequestInactivityTimeoutMs{15000};

/**
 * @brief Orders directory entries with folders first and names in locale-aware order.
 * @param _left First entry to compare.
 * @param _right Second entry to compare.
 * @return true when the first entry should precede the second entry.
 */
bool directoryEntryLess(remote_control::FileEntry const& _left,
                        remote_control::FileEntry const& _right)
{
    if (_left.isDirectory != _right.isDirectory)
    {
        return _left.isDirectory;
    }
    return QString::localeAwareCompare(_left.fileName, _right.fileName) < 0;
}

/**
 * @brief Shuts down a worker and its event loop in one queued callback, then joins the thread.
 * @param _thread Worker thread to stop and join.
 * @param _worker Worker whose thread-owned resources must be released first.
 */
template <typename Worker>
void stopWorkerThread(QThread* const _thread, Worker* const _worker)
{
    if (!_thread->isRunning())
    {
        return;
    }

    bool const shutdownQueued{QMetaObject::invokeMethod(
        _worker,
        [_worker] {
            _worker->shutdown();
            QThread::currentThread()->quit();
        },
        Qt::QueuedConnection)};
    if (!shutdownQueued)
    {
        // The worker context is unavailable; stop the event loop so thread teardown can continue.
        _thread->quit();
    }
    _thread->wait();
}

}  // namespace

/**
 * @brief Executes one asynchronous short-lived request over a dedicated TCP connection.
 *
 * The request sends one protocol command, collects its single-packet or multi-packet response,
 * reports the result through its owning RemoteClient, and then releases itself.
 */
class OneShotRequest final : public QObject
{
    Q_DECLARE_TR_FUNCTIONS(OneShotRequest)

public:
    /**
     * @brief Creates one asynchronous request and its response state.
     * @param _client Client that receives request results.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     * @param _generation Endpoint generation captured when the request starts.
     * @param _command Protocol command to send.
     * @param _payload Command-specific payload.
     * @param _context Command-specific path or label.
     */
    OneShotRequest(RemoteClient* _client,
                   QString const& _host,
                   quint16 _port,
                   quint64 _generation,
                   remote_control::Command _command,
                   QByteArray _payload,
                   QString _context)
        : QObject{_client},
          m_client{_client},
          m_host{_host},
          m_port{_port},
          m_generation{_generation},
          m_command{_command},
          m_payload{std::move(_payload)},
          m_context{std::move(_context)},
          m_timeoutTimer{new QTimer{this}}
    {
        this->m_socket.setParent(this);
        connect(&this->m_socket, &QTcpSocket::connected, this, &OneShotRequest::onConnected);
        connect(&this->m_socket, &QTcpSocket::readyRead, this, &OneShotRequest::onReadyRead);
        connect(&this->m_socket, &QTcpSocket::disconnected, this, &OneShotRequest::onDisconnected);
        connect(
            &this->m_socket, &QTcpSocket::errorOccurred, this, &OneShotRequest::onErrorOccurred);
        this->m_timeoutTimer->setSingleShot(true);
        this->m_timeoutTimer->setInterval(RequestInactivityTimeoutMs);
        connect(this->m_timeoutTimer, &QTimer::timeout, this, [this] {
            CallbackScope const scope{this};
            if (!this->isCurrentGeneration())
            {
                this->discardStaleResult();
                return;
            }
            this->fail(tr("The remote request timed out."));
        });
    }

    /** @brief Connects the one-shot request to the server. */
    void start()
    {
        this->m_timeoutTimer->start();
        this->m_socket.connectToHost(this->m_host, this->m_port);
    }

private:
    /** @brief Sends the serialized request after connecting. */
    void onConnected()
    {
        CallbackScope const scope{this};
        this->m_timeoutTimer->start();
        remote_control::Packet const packet{this->m_command, this->m_payload};
        this->m_socket.write(packet.serialize());
    }

    /** @brief Parses and dispatches available response packets. */
    void onReadyRead()
    {
        CallbackScope const scope{this};
        this->m_timeoutTimer->start();
        // 1. Preserve unread bytes so packets split across TCP reads can be reconstructed.
        this->m_buffer.append(this->m_socket.readAll());
        // 2. Dispatch every complete packet currently available for this request.
        while (true)
        {
            auto const packet{remote_control::Packet::tryParse(this->m_buffer)};
            if (!packet.has_value())
            {
                break;
            }
            this->handlePacket(packet.value());
            if (this->hasFinished())
            {
                break;
            }
        }
        // 3. Bound incomplete responses as well as fully parsed protocol packets.
        if (!this->hasFinished() &&
            this->m_buffer.size() > remote_control::Packet::MaximumSerializedSize)
        {
            this->fail(tr("The remote response exceeds the maximum packet size."));
        }
    }

    /** @brief Completes cleanup or reports an incomplete response. */
    void onDisconnected()
    {
        CallbackScope const scope{this};
        if (!this->isCurrentGeneration())
        {
            this->discardStaleResult();
            return;
        }
        if (this->hasFinished())
        {
            this->requestDeletion();
            return;
        }

        switch (this->m_command)
        {
            case remote_control::Command::ListDirectory:
                this->fail(
                    tr("Directory listing ended before the terminating packet was received."));
                break;
            default:
                this->fail(tr("Connection closed before a complete response was received."));
                break;
        }
    }

    /**
     * @brief Reports a socket error for an unfinished request.
     * @param _error Socket error reported by Qt.
     */
    void onErrorOccurred(QAbstractSocket::SocketError _error)
    {
        CallbackScope const scope{this};
        static_cast<void>(_error);
        if (this->hasFinished())
        {
            return;
        }
        if (!this->isCurrentGeneration())
        {
            this->discardStaleResult();
            return;
        }
        this->fail(this->m_socket.errorString());
    }

    /** @brief Lifecycle states of one short-lived request. */
    enum class RequestState
    {
        Active,             ///< The request is awaiting its complete response.
        Finished,           ///< The result is complete and transport cleanup may continue.
        CleanupDeferred,    ///< Deletion was requested while a callback remained active.
        DeletionScheduled,  ///< Deferred QObject deletion has been scheduled.
    };

    class CallbackScope final
    {
    public:
        /**
         * @brief Tracks entry into a Qt event callback.
         * @param _request Request whose callback depth is tracked.
         */
        explicit CallbackScope(OneShotRequest* _request) : m_request{_request}
        {
            ++this->m_request->m_callbackDepth;
        }

        /** @brief Leaves the Qt event callback and performs deferred cleanup. */
        ~CallbackScope()
        {
            // UI slots can spin nested event loops; delay deletion until we fully unwind.
            --this->m_request->m_callbackDepth;
            if (this->m_request->m_callbackDepth == 0 &&
                this->m_request->m_state == RequestState::CleanupDeferred)
            {
                this->m_request->requestDeletion();
            }
        }

    private:
        OneShotRequest* m_request{nullptr};  ///< Request whose callback depth is guarded.
    };

    /** @brief Deletes the request after active callbacks have unwound. */
    void requestDeletion()
    {
        if (this->m_state == RequestState::DeletionScheduled)
        {
            return;
        }
        if (this->m_callbackDepth > 0)
        {
            this->m_state = RequestState::CleanupDeferred;
            return;
        }
        this->m_state = RequestState::DeletionScheduled;
        deleteLater();
    }

    /**
     * @brief Checks whether response processing has completed.
     * @return true after the request leaves its active state; otherwise false.
     */
    [[nodiscard]] bool hasFinished() const noexcept
    {
        return this->m_state != RequestState::Active;
    }

    /** @brief Marks the request complete. */
    void markFinished()
    {
        if (this->hasFinished())
        {
            return;
        }
        this->m_state = RequestState::Finished;
        this->m_timeoutTimer->stop();
    }

    /**
     * @brief Checks whether this request still belongs to the configured endpoint.
     * @return true when its generation is current; otherwise false.
     */
    [[nodiscard]] bool isCurrentGeneration() const noexcept
    {
        return this->m_client->isEndpointGenerationCurrent(this->m_generation);
    }

    /** @brief Silently releases a result produced for an obsolete endpoint. */
    void discardStaleResult()
    {
        if (this->hasFinished())
        {
            this->requestDeletion();
            return;
        }
        this->markFinished();
        this->m_socket.abort();
        this->requestDeletion();
    }

    /**
     * @brief Dispatches one validated response packet.
     * @param _packet Response packet to dispatch.
     */
    void handlePacket(remote_control::Packet const& _packet)
    {
        if (!this->isCurrentGeneration())
        {
            this->discardStaleResult();
            return;
        }

        if (_packet.command != this->m_command)
        {
            this->fail(
                tr("Received an unexpected command: %1").arg(static_cast<int>(_packet.command)));
            return;
        }

        switch (this->m_command)
        {
            case remote_control::Command::TestConnection:
                this->markFinished();
                emit this->m_client->connectionTested(true, tr("Connection succeeded."));
                this->m_socket.disconnectFromHost();
                break;
            case remote_control::Command::ListDrives:
                this->markFinished();
                emit this->m_client->driveListFinished(
                    remote_control::decodeUtf8(_packet.payload).split(',', Qt::SkipEmptyParts),
                    true,
                    tr("Drive list loaded."));
                this->m_socket.disconnectFromHost();
                break;
            case remote_control::Command::ListDirectory:
                this->handleDirectoryPacket(_packet.payload);
                break;
            case remote_control::Command::RunFile:
            case remote_control::Command::DeleteFile:
                this->handleFileStatusPacket(_packet.payload);
                break;
            default:
                this->fail(tr("Unsupported short-lived request command."));
                break;
        }
    }

    /**
     * @brief Accumulates one streamed directory entry.
     * @param _payload Serialized file-entry payload.
     */
    void handleDirectoryPacket(QByteArray const& _payload)
    {
        remote_control::FileEntry const entry{remote_control::FileEntry::fromPayload(_payload)};
        if (entry.isInvalid)
        {
            this->fail(tr("Directory is unavailable: %1").arg(this->m_context));
            return;
        }

        // The server streams entries one packet at a time and finishes with a marker packet.
        if (entry.hasNext)
        {
            this->m_entries.push_back(entry);
            return;
        }

        // The server enumerates incrementally, so restore the stable UI order after collection.
        std::sort(this->m_entries.begin(), this->m_entries.end(), directoryEntryLess);
        this->markFinished();
        emit this->m_client->directoryListFinished(
            this->m_context, this->m_entries, true, tr("Directory loaded."));
        this->m_socket.disconnectFromHost();
    }

    /**
     * @brief Handles a RunFile or DeleteFile status response.
     * @param _payload Serialized command-status payload.
     */
    void handleFileStatusPacket(QByteArray const& _payload)
    {
        QString message;
        bool const success{remote_control::parseStatusPayload(_payload, &message)};
        if (!success)
        {
            this->fail(message.isEmpty() ? tr("The command failed.") : message);
            return;
        }

        this->markFinished();
        QString const resultMessage{message.isEmpty() ? tr("The command completed successfully.")
                                                      : message};
        emit this->m_client->remotePathCommandFinished(
            this->m_command, this->m_context, true, resultMessage);
        this->m_socket.disconnectFromHost();
    }

    /**
     * @brief Reports failure and releases request resources.
     * @param _message User-facing failure message.
     */
    void fail(QString const& _message)
    {
        if (this->hasFinished())
        {
            return;
        }

        // 1. Mark completion before signals can re-enter the request through Qt.
        this->markFinished();
        switch (this->m_command)
        {
            case remote_control::Command::TestConnection:
                emit this->m_client->connectionTested(false, _message);
                break;
            case remote_control::Command::ListDrives:
                emit this->m_client->driveListFinished({}, false, _message);
                break;
            case remote_control::Command::ListDirectory:
                emit this->m_client->directoryListFinished(this->m_context, {}, false, _message);
                break;
            case remote_control::Command::RunFile:
            case remote_control::Command::DeleteFile:
                emit this->m_client->remotePathCommandFinished(
                    this->m_command, this->m_context, false, _message);
                break;
            default:
                break;
        }

        // 2. Stop transport and defer deletion until active callbacks unwind.
        this->m_socket.abort();
        this->requestDeletion();
    }

    RemoteClient* m_client{nullptr};  ///< Receives the completed request result.
    QString m_host;                   ///< Server host name or address.
    quint16 m_port{0};                ///< Server TCP port.
    quint64 m_generation{0};          ///< Endpoint generation captured at request start.
    remote_control::Command m_command{
        remote_control::Command::TestConnection};  ///< Command sent by this request.
    QByteArray m_payload;                          ///< Command-specific payload.
    QString m_context;                             ///< Result path or label.
    QTcpSocket m_socket;                           ///< Socket dedicated to this request.
    QTimer* m_timeoutTimer{nullptr};               ///< Detects request inactivity.
    QByteArray m_buffer;                           ///< Unparsed received bytes.
    QList<remote_control::FileEntry> m_entries;    ///< Accumulated directory entries.
    RequestState m_state{RequestState::Active};    ///< Current request lifecycle state.
    int m_callbackDepth{0};                        ///< Number of active nested Qt callbacks.
};

RemoteClient::RemoteClient(QObject* _parent)
    : QObject{_parent},
      m_screenStreamThread{new QThread{this}},
      m_screenStreamWorker{new ScreenStreamWorker{}},
      m_controlStreamThread{new QThread{this}},
      m_controlStreamWorker{new ControlStreamWorker{}},
      m_fileDownloadThread{new QThread{this}},
      m_fileDownloadWorker{new FileDownloadWorker{}}
{
    qRegisterMetaType<remote_control::Command>();

    this->m_screenStreamWorker->moveToThread(this->m_screenStreamThread);
    connect(this->m_screenStreamThread,
            &QThread::finished,
            this->m_screenStreamWorker,
            &QObject::deleteLater);
    // Generation checks suppress callbacks emitted by a request that belonged to an old endpoint
    // or to a screen stream that has since been stopped and restarted.
    connect(this->m_screenStreamWorker,
            &ScreenStreamWorker::frameReady,
            this,
            [this](quint64 _generation, QImage const& _image) {
                if (_generation == this->m_screenStreamGeneration)
                {
                    emit this->screenFrameReady(_image);
                }
            });
    connect(this->m_screenStreamWorker,
            &ScreenStreamWorker::failed,
            this,
            [this](quint64 _generation, QString const& _message) {
                if (_generation == this->m_screenStreamGeneration)
                {
                    emit this->screenStreamFailed(_message);
                }
            });
    connect(this->m_screenStreamWorker,
            &ScreenStreamWorker::requestFinished,
            this,
            [this](quint64 _generation) {
                if (_generation == this->m_screenStreamGeneration)
                {
                    this->setScreenFramePending(false);
                    emit this->screenFrameRequestFinished();
                }
            });

    this->m_controlStreamWorker->moveToThread(this->m_controlStreamThread);
    connect(this->m_controlStreamThread,
            &QThread::finished,
            this->m_controlStreamWorker,
            &QObject::deleteLater);
    // Control results are forwarded only while they belong to the current persistent channel.
    connect(this->m_controlStreamWorker,
            &ControlStreamWorker::commandCompleted,
            this,
            [this](quint64 _generation,
                   remote_control::Command _command,
                   QString const& _context,
                   QString const& _message) {
                if (_generation == this->m_controlStreamGeneration)
                {
                    emit this->controlCommandFinished(_command, _context, true, _message);
                }
            });
    connect(this->m_controlStreamWorker,
            &ControlStreamWorker::commandFailed,
            this,
            [this](quint64 _generation,
                   remote_control::Command _command,
                   QString const& _context,
                   QString const& _message) {
                if (_generation == this->m_controlStreamGeneration)
                {
                    emit this->controlCommandFinished(_command, _context, false, _message);
                }
            });

    this->m_fileDownloadWorker->moveToThread(this->m_fileDownloadThread);
    connect(this->m_fileDownloadThread,
            &QThread::finished,
            this->m_fileDownloadWorker,
            &QObject::deleteLater);
    connect(this->m_fileDownloadWorker,
            &FileDownloadWorker::progress,
            this,
            [this](quint64 _endpointGeneration,
                   quint64 _downloadGeneration,
                   QString const& _remotePath,
                   qint64 _received,
                   qint64 _total) {
                if (this->isDownloadGenerationCurrent(_endpointGeneration, _downloadGeneration))
                {
                    emit this->downloadProgress(_remotePath, _received, _total);
                }
            });
    connect(this->m_fileDownloadWorker,
            &FileDownloadWorker::finished,
            this,
            [this](quint64 _endpointGeneration,
                   quint64 _downloadGeneration,
                   QString const& _remotePath,
                   QString const& _localPath,
                   bool _success,
                   QString const& _message) {
                if (this->isDownloadGenerationCurrent(_endpointGeneration, _downloadGeneration))
                {
                    emit this->downloadFinished(_remotePath, _localPath, _success, _message);
                }
            });

    this->m_screenStreamThread->start();
    this->m_controlStreamThread->start();
    this->m_fileDownloadThread->start();
}

RemoteClient::~RemoteClient()
{
    // Each callback releases thread-owned resources before stopping its own event loop.
    stopWorkerThread(this->m_screenStreamThread, this->m_screenStreamWorker);
    stopWorkerThread(this->m_controlStreamThread, this->m_controlStreamWorker);
    stopWorkerThread(this->m_fileDownloadThread, this->m_fileDownloadWorker);
}

void RemoteClient::setEndpoint(QString const& _host, quint16 _port)
{
    // Changing either endpoint component invalidates every persistent stream and its callbacks.
    bool const endpointChanged{this->m_host != _host || this->m_port != _port};
    bool const screenFrameRequestWasPending{endpointChanged && this->hasPendingScreenFrame()};
    if (endpointChanged)
    {
        ++this->m_endpointGeneration;
        ++this->m_downloadGeneration;
        this->stopScreenStream();
        this->stopControlStream();
        quint64 const endpointGeneration{this->m_endpointGeneration};
        quint64 const downloadGeneration{this->m_downloadGeneration};
        QMetaObject::invokeMethod(
            this->m_fileDownloadWorker,
            [worker = this->m_fileDownloadWorker, endpointGeneration, downloadGeneration] {
                worker->cancelActiveDownload(endpointGeneration, downloadGeneration);
            },
            Qt::QueuedConnection);
    }
    this->m_host = _host;
    this->m_port = _port;
    if (screenFrameRequestWasPending)
    {
        // The obsolete worker completion will be discarded, so release the visible scheduler now.
        emit this->screenFrameRequestFinished();
    }
}

void RemoteClient::testConnection()
{
    auto* const request{new OneShotRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::TestConnection,
                                           {},
                                           tr("Connection test")}};
    request->start();
}

void RemoteClient::requestDriveList()
{
    auto* const request{new OneShotRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::ListDrives,
                                           {},
                                           tr("Drive list")}};
    request->start();
}

void RemoteClient::requestDirectoryListing(QString const& _path)
{
    auto* const request{new OneShotRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::ListDirectory,
                                           remote_control::encodeUtf8(_path),
                                           _path}};
    request->start();
}

void RemoteClient::openRemoteFile(QString const& _path)
{
    auto* const request{new OneShotRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::RunFile,
                                           remote_control::encodeUtf8(_path),
                                           _path}};
    request->start();
}

void RemoteClient::deleteRemotePath(QString const& _path)
{
    auto* const request{new OneShotRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::DeleteFile,
                                           remote_control::encodeUtf8(_path),
                                           _path}};
    request->start();
}

void RemoteClient::downloadRemoteFile(QString const& _remotePath, QString const& _localPath)
{
    ++this->m_downloadGeneration;
    QString const host{this->m_host};
    quint16 const port{this->m_port};
    quint64 const endpointGeneration{this->m_endpointGeneration};
    quint64 const downloadGeneration{this->m_downloadGeneration};
    // Queue the download startup on the worker's thread without blocking the caller.
    // QueuedConnection posts the lambda to the worker thread's event loop and returns immediately.
    QMetaObject::invokeMethod(
        this->m_fileDownloadWorker,
        [worker = this->m_fileDownloadWorker,
         host,
         port,
         _remotePath,
         _localPath,
         endpointGeneration,
         downloadGeneration] {
            worker->startDownload(
                host, port, _remotePath, _localPath, endpointGeneration, downloadGeneration);
        },
        Qt::QueuedConnection);
}

void RemoteClient::requestScreenFrame()
{
    // The scheduler must wait for requestFinished before starting another frame request.
    if (this->hasPendingScreenFrame())
    {
        return;
    }
    this->setScreenFramePending(true);
    QString const host{this->m_host};
    quint16 const port{this->m_port};
    quint64 const generation{this->m_screenStreamGeneration};
    QMetaObject::invokeMethod(
        this->m_screenStreamWorker,
        [worker = this->m_screenStreamWorker, host, port, generation] {
            worker->requestFrame(host, port, generation);
        },
        Qt::QueuedConnection);
}

void RemoteClient::stopScreenStream()
{
    ++this->m_screenStreamGeneration;
    this->setScreenFramePending(false);
    QMetaObject::invokeMethod(
        this->m_screenStreamWorker,
        [worker = this->m_screenStreamWorker] { worker->closeConnection(); },
        Qt::QueuedConnection);
}

void RemoteClient::stopControlStream()
{
    ++this->m_controlStreamGeneration;
    QMetaObject::invokeMethod(
        this->m_controlStreamWorker,
        [worker = this->m_controlStreamWorker] { worker->closeConnection(); },
        Qt::QueuedConnection);
}

bool RemoteClient::hasPendingScreenFrame() const noexcept
{
    return this->m_screenFramePending;
}

bool RemoteClient::isEndpointGenerationCurrent(quint64 _generation) const noexcept
{
    return _generation == this->m_endpointGeneration;
}

bool RemoteClient::isDownloadGenerationCurrent(quint64 _endpointGeneration,
                                               quint64 _downloadGeneration) const noexcept
{
    return this->isEndpointGenerationCurrent(_endpointGeneration) &&
        _downloadGeneration == this->m_downloadGeneration;
}

void RemoteClient::setScreenFramePending(bool _pending)
{
    this->m_screenFramePending = _pending;
}

void RemoteClient::sendMouseEvent(remote_control::MouseEventPacket const& _event)
{
    QString const host{this->m_host};
    quint16 const port{this->m_port};
    quint64 const generation{this->m_controlStreamGeneration};
    QMetaObject::invokeMethod(
        this->m_controlStreamWorker,
        [worker = this->m_controlStreamWorker, host, port, _event, generation] {
            worker->sendMouseEvent(host, port, _event, generation);
        },
        Qt::QueuedConnection);
}

void RemoteClient::lockRemote()
{
    QString const host{this->m_host};
    quint16 const port{this->m_port};
    QString const context{tr("Lock")};
    quint64 const generation{this->m_controlStreamGeneration};
    QMetaObject::invokeMethod(
        this->m_controlStreamWorker,
        [worker = this->m_controlStreamWorker, host, port, context, generation] {
            worker->sendCommand(
                host, port, remote_control::Command::LockMachine, context, generation);
        },
        Qt::QueuedConnection);
}

void RemoteClient::unlockRemote()
{
    QString const host{this->m_host};
    quint16 const port{this->m_port};
    QString const context{tr("Unlock")};
    quint64 const generation{this->m_controlStreamGeneration};
    QMetaObject::invokeMethod(
        this->m_controlStreamWorker,
        [worker = this->m_controlStreamWorker, host, port, context, generation] {
            worker->sendCommand(
                host, port, remote_control::Command::UnlockMachine, context, generation);
        },
        Qt::QueuedConnection);
}
