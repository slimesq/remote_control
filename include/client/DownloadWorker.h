#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

class QSaveFile;
class QTcpSocket;
class QTimer;

/** @brief Receives and writes one streamed download outside the client GUI thread. */
class DownloadWorker final : public QObject
{
    Q_OBJECT

public:
    /** @brief Creates an idle download worker. */
    explicit DownloadWorker();

    /** @brief Cancels active work and releases worker-owned resources. */
    ~DownloadWorker() override;

public slots:
    /**
     * @brief Starts one streamed file download.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     * @param _remotePath Source path on the remote host.
     * @param _localPath Destination path on the local host.
     */
    void startDownload(QString const& _host,
                       quint16 _port,
                       QString const& _remotePath,
                       QString const& _localPath);

    /** @brief Cancels active work before the download thread exits. */
    void shutdown();

signals:
    /**
     * @brief Reports received download bytes.
     * @param _remotePath Downloaded remote path.
     * @param _received Number of bytes written locally.
     * @param _total Expected total byte count.
     */
    void progress(QString const& _remotePath, qint64 _received, qint64 _total);

    /**
     * @brief Reports the final download result.
     * @param _remotePath Downloaded remote path.
     * @param _localPath Local destination path.
     * @param _success Whether the download succeeded.
     * @param _message User-facing result message.
     */
    void finished(QString const& _remotePath,
                  QString const& _localPath,
                  bool _success,
                  QString const& _message);

private:
    /** @brief Lifecycle states of the reusable download worker. */
    enum class DownloadState
    {
        Idle,          ///< The worker is ready to start a download.
        Downloading,   ///< One download is currently active.
        ShuttingDown,  ///< The worker is releasing resources before thread exit.
    };

    /** @brief Sends the download request after connecting. */
    void onConnected();

    /** @brief Parses and writes all available download packets. */
    void onReadyRead();

    /** @brief Reports an incomplete download after disconnection. */
    void onDisconnected();

    /**
     * @brief Reports a socket error for the active download.
     * @param _error Socket error reported by Qt.
     */
    void onErrorOccurred(QAbstractSocket::SocketError _error);

    /** @brief Reports a stalled download after its inactivity timeout. */
    void onTimeout();

    /**
     * @brief Processes one size-header or file-data packet.
     * @param _payload Download response payload.
     */
    void processPacket(QByteArray const& _payload);

    /** @brief Commits a fully received temporary file and reports success. */
    void completeSuccessfully();

    /**
     * @brief Cancels the temporary file and reports failure.
     * @param _message User-facing failure message.
     */
    void fail(QString const& _message);

    /** @brief Destroys the active socket and clears protocol buffering. */
    void resetSocket();

    QTcpSocket* m_socket{nullptr};      ///< Socket dedicated to the active download.
    QTimer* m_timeoutTimer{nullptr};    ///< Detects inactivity during the active download.
    std::unique_ptr<QSaveFile> m_file;  ///< Transactional local output file.
    QString m_host;                     ///< Server host name or address.
    quint16 m_port{0};                  ///< Server TCP port.
    QString m_remotePath;               ///< Remote source path being downloaded.
    QString m_localPath;                ///< Local destination path being written.
    QByteArray m_buffer;                ///< Bytes waiting to form complete response packets.
    qint64 m_expectedBytes{-1};         ///< Declared file size, or -1 before the size header.
    qint64 m_receivedBytes{0};          ///< Number of file bytes written locally.
    DownloadState m_state{DownloadState::Idle};  ///< Current download lifecycle.
};
