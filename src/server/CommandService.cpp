#include "server/CommandService.h"

#include "server/LockWindow.h"
#include "server/PlatformIntegration.h"

#include <QBuffer>
#include <QDataStream>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPoint>
#include <QScreen>
#include <QTimer>
#include <QUrl>

#include <cstring>

namespace
{

constexpr int MillisecondsPerSecond{1000};
constexpr int DownloadChunkSize{1024};

QByteArray sizeToPayload(qint64 _size)
{
    QByteArray payload;
    QDataStream stream{&payload, QIODevice::WriteOnly};
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << _size;
    return payload;
}

remote_control::Packet statusPacket(remote_control::Command _command,
                                    bool _success,
                                    QString const& _message = {})
{
    return remote_control::Packet{_command, remote_control::makeStatusPayload(_success, _message)};
}

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
            return this->handleListDirectory(_request.payload);
        case remote_control::Command::RunFile:
            return this->handleRunFile(_request.payload);
        case remote_control::Command::DownloadFile:
            return this->handleDownloadFile(_request.payload);
        case remote_control::Command::MouseEvent:
            return this->handleMouseEvent(_request.payload);
        case remote_control::Command::WatchScreen:
            return this->handleWatchScreen();
        case remote_control::Command::LockMachine:
            return this->handleLockMachine();
        case remote_control::Command::UnlockMachine:
            return this->handleUnlockMachine();
        case remote_control::Command::DeleteFile:
            return this->handleDeleteFile(_request.payload);
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

QList<remote_control::Packet> CommandService::handleListDirectory(QByteArray const& _payload) const
{
    QList<remote_control::Packet> packets;
    QString const path{decodePath(_payload)};
    QFileInfo const dirInfo{path};
    if (!dirInfo.exists() || !dirInfo.isDir())
    {
        remote_control::FileEntry entry;
        entry.isInvalid = true;
        entry.hasNext = false;
        packets.append(
            remote_control::Packet{remote_control::Command::ListDirectory, entry.toPayload()});
        return packets;
    }

    QDir const dir{path};
    if (!dir.isReadable())
    {
        remote_control::FileEntry entry;
        entry.isInvalid = true;
        entry.hasNext = false;
        packets.append(
            remote_control::Packet{remote_control::Command::ListDirectory, entry.toPayload()});
        return packets;
    }

    QFileInfoList const entries{
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDir::DirsFirst | QDir::Name)};

    for (QFileInfo const& info : entries)
    {
        remote_control::FileEntry entry;
        entry.isDirectory = info.isDir();
        entry.fileName = info.fileName();
        packets.append(
            remote_control::Packet{remote_control::Command::ListDirectory, entry.toPayload()});
    }

    // A final marker packet tells the client that the streamed listing is complete.
    remote_control::FileEntry endEntry;
    endEntry.hasNext = false;
    packets.append(
        remote_control::Packet{remote_control::Command::ListDirectory, endEntry.toPayload()});
    return packets;
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

QList<remote_control::Packet> CommandService::handleDownloadFile(QByteArray const& _payload) const
{
    QList<remote_control::Packet> packets;
    QFile file{decodePath(_payload)};
    if (!file.open(QIODevice::ReadOnly))
    {
        packets.append(
            remote_control::Packet{remote_control::Command::DownloadFile, sizeToPayload(-1)});
        return packets;
    }

    // Downloads start with a size header so the client can pre-validate and track progress.
    packets.append(
        remote_control::Packet{remote_control::Command::DownloadFile, sizeToPayload(file.size())});
    while (!file.atEnd())
    {
        packets.append(remote_control::Packet{remote_control::Command::DownloadFile,
                                              file.read(DownloadChunkSize)});
    }
    return packets;
}

QList<remote_control::Packet> CommandService::handleMouseEvent(QByteArray const& _payload) const
{
    if (_payload.size() != static_cast<int>(sizeof(remote_control::MouseEventPacket)))
    {
        return {statusPacket(
            remote_control::Command::MouseEvent, false, tr("Invalid mouse event payload."))};
    }

    remote_control::MouseEventPacket event{};
    std::memcpy(&event, _payload.constData(), sizeof(event));
    bool const success{PlatformIntegration::sendGlobalMouseEvent(
        QPoint{event.x, event.y},
        static_cast<remote_control::MouseAction>(event.action),
        static_cast<remote_control::MouseButton>(event.button))};
    return {statusPacket(remote_control::Command::MouseEvent,
                         success,
                         success ? QString{} : tr("Failed to send the mouse event."))};
}

QList<remote_control::Packet> CommandService::handleWatchScreen() const
{
    QList<remote_control::Packet> packets;
    QScreen* const screen{QGuiApplication::primaryScreen()};
    if (!screen)
    {
        packets.append(remote_control::Packet{remote_control::Command::WatchScreen});
        return packets;
    }

    QByteArray payload;
    QBuffer buffer{&payload};
    buffer.open(QIODevice::WriteOnly);
    screen->grabWindow(0).toImage().save(&buffer, "PNG");
    packets.append(remote_control::Packet{remote_control::Command::WatchScreen, payload});
    return packets;
}

QList<remote_control::Packet> CommandService::handleLockMachine()
{
    this->lockLocalMachine();
    return {
        statusPacket(remote_control::Command::LockMachine, true, tr("Lock machine completed."))};
}

QList<remote_control::Packet> CommandService::handleUnlockMachine()
{
    this->unlockLocalMachine();
    return {statusPacket(
        remote_control::Command::UnlockMachine, true, tr("Unlock machine completed."))};
}

QList<remote_control::Packet> CommandService::handleDeleteFile(QByteArray const& _payload) const
{
    QString const path{decodePath(_payload)};
    QFileInfo const info{path};
    if (!info.exists())
    {
        return {statusPacket(
            remote_control::Command::DeleteFile, false, tr("Target does not exist: %1").arg(path))};
    }

    bool success{false};
    if (info.isDir())
    {
        success = QDir(path).removeRecursively();
    }
    else
    {
        success = QFile::remove(path);
    }

    if (!success)
    {
        return {statusPacket(remote_control::Command::DeleteFile,
                             false,
                             tr("Failed to delete target: %1").arg(path))};
    }
    return {statusPacket(remote_control::Command::DeleteFile, true, tr("Delete completed."))};
}

QList<remote_control::Packet> CommandService::handleTestConnection() const
{
    return {remote_control::Packet{remote_control::Command::TestConnection}};
}
