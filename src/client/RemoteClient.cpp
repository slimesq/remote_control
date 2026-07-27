#include "client/RemoteClient.h"

#include "client/ControlConnectionWorker.h"
#include "client/DownloadWorker.h"
#include "client/WatchConnectionWorker.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

namespace
{

constexpr int RequestInactivityTimeoutMs{15000};

}  // namespace

/**
 * @brief Executes one asynchronous short-lived request over a dedicated TCP connection.
 *
 * The request sends one protocol command, collects its single-packet or multi-packet response,
 * reports the result through its owning RemoteClient, and then releases itself.
 */
class PendingRequest final : public QObject
{
    Q_DECLARE_TR_FUNCTIONS(PendingRequest)

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
    PendingRequest(RemoteClient* _client,
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
        connect(&this->m_socket, &QTcpSocket::connected, this, &PendingRequest::onConnected);
        connect(&this->m_socket, &QTcpSocket::readyRead, this, &PendingRequest::onReadyRead);
        connect(&this->m_socket, &QTcpSocket::disconnected, this, &PendingRequest::onDisconnected);
        connect(
            &this->m_socket, &QTcpSocket::errorOccurred, this, &PendingRequest::onErrorOccurred);
        this->m_timeoutTimer->setSingleShot(true);
        this->m_timeoutTimer->setInterval(RequestInactivityTimeoutMs);
        connect(this->m_timeoutTimer, &QTimer::timeout, this, [this] {
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
        this->m_timeoutTimer->start();
        remote_control::Packet const packet{this->m_command, this->m_payload};
        this->m_socket.write(packet.serialize());
    }

    /** @brief Parses and dispatches available response packets. */
    void onReadyRead()
    {
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
            if (this->m_finished)
            {
                break;
            }
        }
        // 3. Bound incomplete responses as well as fully parsed protocol packets.
        if (!this->m_finished &&
            this->m_buffer.size() > remote_control::Packet::MaximumSerializedSize)
        {
            this->fail(tr("The remote response exceeds the maximum packet size."));
        }
    }

    /** @brief Completes cleanup or reports an incomplete response. */
    void onDisconnected()
    {
        if (!this->isCurrentGeneration())
        {
            this->discardStaleResult();
            return;
        }
        if (this->m_finished)
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
        static_cast<void>(_error);
        if (this->m_finished)
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

    class CallbackScope final
    {
    public:
        /**
         * @brief Tracks entry into a request callback.
         * @param _request Request whose callback depth is tracked.
         */
        explicit CallbackScope(PendingRequest* _request) : m_request{_request}
        {
            ++this->m_request->m_callbackDepth;
        }

        /** @brief Leaves the callback and performs deferred cleanup. */
        ~CallbackScope()
        {
            // UI slots can spin nested event loops; delay deletion until we fully unwind.
            --this->m_request->m_callbackDepth;
            if (this->m_request->m_callbackDepth == 0 && this->m_request->m_cleanupPending)
            {
                this->m_request->m_cleanupPending = false;
                this->m_request->deleteLater();
            }
        }

    private:
        PendingRequest* m_request{nullptr};
    };

    /** @brief Deletes the request after active callbacks have unwound. */
    void requestDeletion()
    {
        if (this->m_callbackDepth > 0)
        {
            this->m_cleanupPending = true;
            return;
        }
        deleteLater();
    }

    /** @brief Marks the request complete. */
    void markFinished()
    {
        if (this->m_finished)
        {
            return;
        }
        this->m_finished = true;
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
        if (this->m_finished)
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
        CallbackScope const scope{this};

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
            case remote_control::Command::LockMachine:
            case remote_control::Command::UnlockMachine:
            case remote_control::Command::MouseEvent:
                this->handleStatusPacket(_packet.payload);
                break;
            case remote_control::Command::DownloadFile:
                this->fail(tr("Downloads require the dedicated transfer worker."));
                break;
            case remote_control::Command::WatchScreen:
                this->fail(tr("Monitor frames require the persistent watch connection."));
                break;
            case remote_control::Command::ControlChannel:
                this->fail(tr("Control commands require the persistent control connection."));
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

        this->markFinished();
        emit this->m_client->directoryListFinished(
            this->m_context, this->m_entries, true, tr("Directory loaded."));
        this->m_socket.disconnectFromHost();
    }

    /**
     * @brief Handles a common command-status response.
     * @param _payload Serialized command-status payload.
     */
    void handleStatusPacket(QByteArray const& _payload)
    {
        QString message;
        bool const success{remote_control::parseStatusPayload(_payload, true, &message)};
        if (!success)
        {
            this->fail(message.isEmpty() ? tr("The command failed.") : message);
            return;
        }

        this->markFinished();
        QString const resultMessage{message.isEmpty() ? tr("The command completed successfully.")
                                                      : message};
        if (this->m_command == remote_control::Command::RunFile ||
            this->m_command == remote_control::Command::DeleteFile)
        {
            emit this->m_client->fileCommandFinished(
                this->m_command, this->m_context, true, resultMessage);
        }
        else
        {
            emit this->m_client->controlCommandFinished(
                this->m_command, this->m_context, true, resultMessage);
        }
        this->m_socket.disconnectFromHost();
    }

    /**
     * @brief Reports failure and releases request resources.
     * @param _message User-facing failure message.
     */
    void fail(QString const& _message)
    {
        if (this->m_finished)
        {
            return;
        }

        CallbackScope const scope{this};

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
            case remote_control::Command::DownloadFile:
                emit this->m_client->downloadFinished(this->m_context, {}, false, _message);
                break;
            case remote_control::Command::WatchScreen:
                emit this->m_client->watchFailed(_message);
                break;
            case remote_control::Command::RunFile:
            case remote_control::Command::DeleteFile:
                emit this->m_client->fileCommandFinished(
                    this->m_command, this->m_context, false, _message);
                break;
            case remote_control::Command::LockMachine:
            case remote_control::Command::UnlockMachine:
            case remote_control::Command::MouseEvent:
            case remote_control::Command::ControlChannel:
                emit this->m_client->controlCommandFinished(
                    this->m_command, this->m_context, false, _message);
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
    bool m_finished{false};                        ///< Whether the request has finished.
    int m_callbackDepth{0};                        ///< Number of active nested callbacks.
    bool m_cleanupPending{false};                  ///< Whether cleanup must be deferred.
};

RemoteClient::RemoteClient(QObject* _parent)
    : QObject{_parent},
      m_watchThread{new QThread{this}},
      m_watchWorker{new WatchConnectionWorker{}},
      m_controlThread{new QThread{this}},
      m_controlWorker{new ControlConnectionWorker{}},
      m_downloadThread{new QThread{this}},
      m_downloadWorker{new DownloadWorker{}}
{
    qRegisterMetaType<remote_control::Command>();
    qRegisterMetaType<remote_control::FileEntry>();
    qRegisterMetaType<QList<remote_control::FileEntry>>();

    this->m_watchWorker->moveToThread(this->m_watchThread);
    connect(this->m_watchThread, &QThread::finished, this->m_watchWorker, &QObject::deleteLater);
    connect(this->m_watchWorker,
            &WatchConnectionWorker::frameReady,
            this,
            [this](quint64 _generation, QImage const& _image) {
                if (_generation == this->m_watchGeneration)
                {
                    emit this->watchFrameReady(_image);
                }
            });
    connect(this->m_watchWorker,
            &WatchConnectionWorker::failed,
            this,
            [this](quint64 _generation, QString const& _message) {
                if (_generation == this->m_watchGeneration)
                {
                    emit this->watchFailed(_message);
                }
            });
    connect(this->m_watchWorker,
            &WatchConnectionWorker::requestFinished,
            this,
            [this](quint64 _generation) {
                if (_generation == this->m_watchGeneration)
                {
                    this->setWatchFramePending(false);
                }
            });

    this->m_controlWorker->moveToThread(this->m_controlThread);
    connect(
        this->m_controlThread, &QThread::finished, this->m_controlWorker, &QObject::deleteLater);
    connect(this->m_controlWorker,
            &ControlConnectionWorker::commandCompleted,
            this,
            [this](quint64 _generation,
                   remote_control::Command _command,
                   QString const& _context,
                   QString const& _message) {
                if (_generation == this->m_controlGeneration)
                {
                    emit this->controlCommandFinished(_command, _context, true, _message);
                }
            });
    connect(this->m_controlWorker,
            &ControlConnectionWorker::commandFailed,
            this,
            [this](quint64 _generation,
                   remote_control::Command _command,
                   QString const& _context,
                   QString const& _message) {
                if (_generation == this->m_controlGeneration)
                {
                    emit this->controlCommandFinished(_command, _context, false, _message);
                }
            });

    this->m_downloadWorker->moveToThread(this->m_downloadThread);
    connect(
        this->m_downloadThread, &QThread::finished, this->m_downloadWorker, &QObject::deleteLater);
    connect(
        this->m_downloadWorker, &DownloadWorker::progress, this, &RemoteClient::downloadProgress);
    connect(this->m_downloadWorker,
            &DownloadWorker::finished,
            this,
            [this](QString const& _remotePath,
                   QString const& _localPath,
                   bool _success,
                   QString const& _message) {
                emit this->downloadFinished(_remotePath, _localPath, _success, _message);
            });

    this->m_watchThread->start();
    this->m_controlThread->start();
    this->m_downloadThread->start();
}

RemoteClient::~RemoteClient()
{
    if (this->m_watchThread->isRunning())
    {
        QMetaObject::invokeMethod(
            this->m_watchWorker,
            [worker = this->m_watchWorker] { worker->shutdown(); },
            Qt::BlockingQueuedConnection);
        this->m_watchThread->quit();
        this->m_watchThread->wait();
    }

    if (this->m_controlThread->isRunning())
    {
        QMetaObject::invokeMethod(
            this->m_controlWorker,
            [worker = this->m_controlWorker] { worker->shutdown(); },
            Qt::BlockingQueuedConnection);
        this->m_controlThread->quit();
        this->m_controlThread->wait();
    }

    if (this->m_downloadThread->isRunning())
    {
        QMetaObject::invokeMethod(
            this->m_downloadWorker,
            [worker = this->m_downloadWorker] { worker->shutdown(); },
            Qt::BlockingQueuedConnection);
        this->m_downloadThread->quit();
        this->m_downloadThread->wait();
    }
}

void RemoteClient::setEndpoint(QString const& _host, quint16 _port)
{
    if (this->m_host != _host || this->m_port != _port)
    {
        ++this->m_endpointGeneration;
        this->stopWatchStream();
        this->stopControlStream();
    }
    this->m_host = _host;
    this->m_port = _port;
}

void RemoteClient::testConnection()
{
    auto* const request{new PendingRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::TestConnection,
                                           {},
                                           tr("Connection test")}};
    request->start();
}

void RemoteClient::requestDrives()
{
    auto* const request{new PendingRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::ListDrives,
                                           {},
                                           tr("Drive list")}};
    request->start();
}

void RemoteClient::requestDirectory(QString const& _path)
{
    auto* const request{new PendingRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::ListDirectory,
                                           remote_control::encodeUtf8(_path),
                                           _path}};
    request->start();
}

void RemoteClient::runFile(QString const& _path)
{
    auto* const request{new PendingRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::RunFile,
                                           remote_control::encodeUtf8(_path),
                                           _path}};
    request->start();
}

void RemoteClient::deleteFile(QString const& _path)
{
    auto* const request{new PendingRequest{this,
                                           this->m_host,
                                           this->m_port,
                                           this->m_endpointGeneration,
                                           remote_control::Command::DeleteFile,
                                           remote_control::encodeUtf8(_path),
                                           _path}};
    request->start();
}

void RemoteClient::downloadFile(QString const& _remotePath, QString const& _localPath)
{
    QString const host{this->m_host};
    quint16 const port{this->m_port};
    // Queue the download startup on the worker's thread without blocking the caller.
    // QueuedConnection posts the lambda to the worker thread's event loop and returns immediately.
    QMetaObject::invokeMethod(
        this->m_downloadWorker,
        [worker = this->m_downloadWorker, host, port, _remotePath, _localPath] {
            worker->startDownload(host, port, _remotePath, _localPath);
        },
        Qt::QueuedConnection);
}

void RemoteClient::requestWatchFrame()
{
    if (this->hasPendingWatchFrame())
    {
        return;
    }
    this->setWatchFramePending(true);
    QString const host{this->m_host};
    quint16 const port{this->m_port};
    quint64 const generation{this->m_watchGeneration};
    QMetaObject::invokeMethod(
        this->m_watchWorker,
        [worker = this->m_watchWorker, host, port, generation] {
            worker->requestFrame(host, port, generation);
        },
        Qt::QueuedConnection);
}

void RemoteClient::stopWatchStream()
{
    ++this->m_watchGeneration;
    this->setWatchFramePending(false);
    QMetaObject::invokeMethod(
        this->m_watchWorker,
        [worker = this->m_watchWorker] { worker->closeConnection(); },
        Qt::QueuedConnection);
}

void RemoteClient::stopControlStream()
{
    ++this->m_controlGeneration;
    QMetaObject::invokeMethod(
        this->m_controlWorker,
        [worker = this->m_controlWorker] { worker->closeConnection(); },
        Qt::QueuedConnection);
}

bool RemoteClient::hasPendingWatchFrame() const noexcept
{
    return this->m_watchPending;
}

bool RemoteClient::isEndpointGenerationCurrent(quint64 _generation) const noexcept
{
    return _generation == this->m_endpointGeneration;
}

void RemoteClient::setWatchFramePending(bool _pending)
{
    this->m_watchPending = _pending;
}

void RemoteClient::sendMouseEvent(remote_control::MouseEventPacket const& _event)
{
    QString const host{this->m_host};
    quint16 const port{this->m_port};
    quint64 const generation{this->m_controlGeneration};
    QMetaObject::invokeMethod(
        this->m_controlWorker,
        [worker = this->m_controlWorker, host, port, _event, generation] {
            worker->sendMouseEvent(host, port, _event, generation);
        },
        Qt::QueuedConnection);
}

void RemoteClient::lockRemote()
{
    QString const host{this->m_host};
    quint16 const port{this->m_port};
    QString const context{tr("Lock")};
    quint64 const generation{this->m_controlGeneration};
    QMetaObject::invokeMethod(
        this->m_controlWorker,
        [worker = this->m_controlWorker, host, port, context, generation] {
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
    quint64 const generation{this->m_controlGeneration};
    QMetaObject::invokeMethod(
        this->m_controlWorker,
        [worker = this->m_controlWorker, host, port, context, generation] {
            worker->sendCommand(
                host, port, remote_control::Command::UnlockMachine, context, generation);
        },
        Qt::QueuedConnection);
}
