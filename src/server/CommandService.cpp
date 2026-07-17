#include "server/CommandService.h"

#include "server/LockWindow.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

namespace
{

constexpr int MillisecondsPerSecond{1000};

/**
 * @brief Creates a common command-status packet.
 * @param _command Command associated with the status.
 * @param _success Whether the command succeeded.
 * @param _message Optional user-facing result message.
 * @return Serialized command-status packet.
 */
remote_control::Packet statusPacket(remote_control::Command _command,
                                    bool _success,
                                    QString const& _message = {})
{
    return remote_control::Packet{_command, remote_control::makeStatusPayload(_success, _message)};
}

/**
 * @brief Decodes a path from a protocol payload.
 * @param _payload UTF-8 encoded path payload.
 * @return Decoded path.
 */
QString decodePath(QByteArray const& _payload)
{
    return remote_control::decodeUtf8(_payload);
}

}  // namespace

CommandService::CommandService(QObject* _parent)
    : QObject{_parent},
      m_lockWindow{std::make_unique<LockWindow>()},
      m_lockTestTimer{new QTimer{this}}
{
    this->m_lockTestTimer->setSingleShot(true);
    connect(this->m_lockTestTimer, &QTimer::timeout, this, [this] {
        this->unlockLocalMachine();
        emit this->timedLockTestFinished();
    });
}

CommandService::~CommandService()
{
    this->m_lockTestTimer->stop();
    this->m_lockWindow->unlockMachine();
}

void CommandService::lockLocalMachine()
{
    bool const wasLocked{this->isLocked()};
    this->m_lockWindow->lockMachine();
    if (!wasLocked && this->isLocked())
    {
        emit this->lockStateChanged(true);
    }
}

void CommandService::unlockLocalMachine()
{
    this->m_lockTestTimer->stop();
    bool const wasLocked{this->isLocked()};
    this->m_lockWindow->unlockMachine();
    if (wasLocked && !this->isLocked())
    {
        emit this->lockStateChanged(false);
    }
}

void CommandService::runTimedLockTest(int _seconds)
{
    this->lockLocalMachine();
    this->m_lockTestTimer->start(qMax(1, _seconds) * MillisecondsPerSecond);
}

bool CommandService::isLocked() const noexcept
{
    return this->m_lockWindow->isLocked();
}

QList<remote_control::Packet> CommandService::handle(remote_control::Packet const& _request)
{
    switch (_request.command)
    {
        case remote_control::Command::ListDrives:
            return this->handleListDrives();
        case remote_control::Command::ListDirectory:
            return {};
        case remote_control::Command::RunFile:
            return this->handleRunFile(_request.payload);
        case remote_control::Command::DownloadFile:
            return {};
        case remote_control::Command::MouseEvent:
            return {};
        case remote_control::Command::WatchScreen:
            return {};
        case remote_control::Command::LockMachine:
            return {};
        case remote_control::Command::UnlockMachine:
            return {};
        case remote_control::Command::DeleteFile:
            return {};
        case remote_control::Command::ControlChannel:
            return {};
        case remote_control::Command::TestConnection:
            return this->handleTestConnection();
    }
    return {};
}

QList<remote_control::Packet> CommandService::handleListDrives() const
{
    QStringList drives;
    for (QFileInfo const& drive : QDir::drives())
    {
        QString drivePath{QDir::toNativeSeparators(drive.absoluteFilePath())};
        while (drivePath.endsWith(QDir::separator()))
        {
            drivePath.chop(1);
        }
        if (!drivePath.isEmpty())
        {
            drives.append(drivePath);
        }
    }
    return {remote_control::Packet{remote_control::Command::ListDrives,
                                   remote_control::encodeUtf8(drives.join(','))}};
}

QList<remote_control::Packet> CommandService::handleRunFile(QByteArray const& _payload) const
{
    QString const path{decodePath(_payload)};
    if (!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
    {
        return {statusPacket(
            remote_control::Command::RunFile, false, tr("Failed to open file: %1").arg(path))};
    }
    return {statusPacket(remote_control::Command::RunFile, true, tr("Open file completed."))};
}

QList<remote_control::Packet> CommandService::handleTestConnection() const
{
    return {remote_control::Packet{remote_control::Command::TestConnection}};
}
