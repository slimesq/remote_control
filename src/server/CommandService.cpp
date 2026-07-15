#include "CommandService.h"

#include "LockWindow.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>

#include <windows.h>
#include <shellapi.h>

#include <cstring>

#ifdef DeleteFile
#undef DeleteFile
#endif

namespace {

QByteArray sizeToPayload(qint64 _size)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << _size;
    return payload;
}

remote_control::Packet statusPacket(remote_control::Command _command, bool _success, const QString& _message = {})
{
    return remote_control::Packet(_command, remote_control::makeStatusPayload(_success, _message));
}

QString decodePath(const QByteArray& _payload)
{
    return remote_control::decodeUtf8(_payload);
}

DWORD mouseDownFlag(remote_control::MouseButton _button)
{
    switch (_button) {
    case remote_control::MouseButton::Left:
        return MOUSEEVENTF_LEFTDOWN;
    case remote_control::MouseButton::Right:
        return MOUSEEVENTF_RIGHTDOWN;
    case remote_control::MouseButton::Middle:
        return MOUSEEVENTF_MIDDLEDOWN;
    default:
        return 0;
    }
}

DWORD mouseUpFlag(remote_control::MouseButton _button)
{
    switch (_button) {
    case remote_control::MouseButton::Left:
        return MOUSEEVENTF_LEFTUP;
    case remote_control::MouseButton::Right:
        return MOUSEEVENTF_RIGHTUP;
    case remote_control::MouseButton::Middle:
        return MOUSEEVENTF_MIDDLEUP;
    default:
        return 0;
    }
}

void sendMouseFlag(DWORD _flag)
{
    if (_flag == 0) {
        return;
    }
    INPUT input {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = _flag;
    SendInput(1, &input, sizeof(INPUT));
}

}

CommandService::CommandService(QObject* _parent)
    : QObject(_parent)
    , m_lockWindow(std::make_unique<LockWindow>())
{
}

CommandService::~CommandService()
{
    this->unlockLocalMachine();
}

void CommandService::lockLocalMachine()
{
    this->m_lockWindow->lockMachine();
}

void CommandService::unlockLocalMachine()
{
    this->m_lockWindow->unlockMachine();
}

bool CommandService::isLocked() const
{
    return this->m_lockWindow->isLocked();
}

QList<remote_control::Packet> CommandService::handle(const remote_control::Packet& _request)
{
    switch (_request.command) {
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
    const DWORD mask = GetLogicalDrives();
    for (int index = 0; index < 26; ++index) {
        if ((mask & (1u << index)) != 0) {
            drives << QString(QChar('A' + index));
        }
    }
    return { remote_control::Packet(remote_control::Command::ListDrives, remote_control::encodeUtf8(drives.join(','))) };
}

QList<remote_control::Packet> CommandService::handleListDirectory(const QByteArray& _payload) const
{
    QList<remote_control::Packet> packets;
    const QString path = decodePath(_payload);
    QFileInfo dirInfo(path);
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        remote_control::FileEntry entry;
        entry.isInvalid = true;
        entry.hasNext = false;
        packets.append(remote_control::Packet(remote_control::Command::ListDirectory, entry.toPayload()));
        return packets;
    }

    QDir dir(path);
    if (!dir.isReadable()) {
        remote_control::FileEntry entry;
        entry.isInvalid = true;
        entry.hasNext = false;
        packets.append(remote_control::Packet(remote_control::Command::ListDirectory, entry.toPayload()));
        return packets;
    }

    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::DirsFirst | QDir::Name);

    for (const QFileInfo& info : entries) {
        remote_control::FileEntry entry;
        entry.isDirectory = info.isDir();
        entry.fileName = info.fileName();
        packets.append(remote_control::Packet(remote_control::Command::ListDirectory, entry.toPayload()));
    }

    // A final marker packet tells the client that the streamed listing is complete.
    remote_control::FileEntry endEntry;
    endEntry.hasNext = false;
    packets.append(remote_control::Packet(remote_control::Command::ListDirectory, endEntry.toPayload()));
    return packets;
}

QList<remote_control::Packet> CommandService::handleRunFile(const QByteArray& _payload) const
{
    const QString path = decodePath(_payload);
    const HINSTANCE result = ShellExecuteW(nullptr, nullptr, reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        return { statusPacket(remote_control::Command::RunFile, false, tr("Failed to open file: %1").arg(path)) };
    }
    return { statusPacket(remote_control::Command::RunFile, true, tr("Open file completed.")) };
}

QList<remote_control::Packet> CommandService::handleDownloadFile(const QByteArray& _payload) const
{
    QList<remote_control::Packet> packets;
    QFile file(decodePath(_payload));
    if (!file.open(QIODevice::ReadOnly)) {
        packets.append(remote_control::Packet(remote_control::Command::DownloadFile, sizeToPayload(-1)));
        return packets;
    }

    // Downloads start with a size header so the client can pre-validate and track progress.
    packets.append(remote_control::Packet(remote_control::Command::DownloadFile, sizeToPayload(file.size())));
    while (!file.atEnd()) {
        packets.append(remote_control::Packet(remote_control::Command::DownloadFile, file.read(1024)));
    }
    return packets;
}

QList<remote_control::Packet> CommandService::handleMouseEvent(const QByteArray& _payload) const
{
    if (_payload.size() >= static_cast<int>(sizeof(remote_control::MouseEventPacket))) {
        remote_control::MouseEventPacket event {};
        std::memcpy(&event, _payload.constData(), sizeof(event));
        SetCursorPos(event.x, event.y);

        const auto button = static_cast<remote_control::MouseButton>(event.button);
        const auto action = static_cast<remote_control::MouseAction>(event.action);

        switch (action) {
        case remote_control::MouseAction::Click:
            if (button != remote_control::MouseButton::None) {
                sendMouseFlag(mouseDownFlag(button));
                sendMouseFlag(mouseUpFlag(button));
            }
            break;
        case remote_control::MouseAction::DoubleClick:
            if (button != remote_control::MouseButton::None) {
                sendMouseFlag(mouseDownFlag(button));
                sendMouseFlag(mouseUpFlag(button));
                sendMouseFlag(mouseDownFlag(button));
                sendMouseFlag(mouseUpFlag(button));
            }
            break;
        case remote_control::MouseAction::Press:
            sendMouseFlag(mouseDownFlag(button));
            break;
        case remote_control::MouseAction::Release:
            sendMouseFlag(mouseUpFlag(button));
            break;
        }
    }
    return { statusPacket(remote_control::Command::MouseEvent, true) };
}

QList<remote_control::Packet> CommandService::handleWatchScreen() const
{
    QList<remote_control::Packet> packets;
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        packets.append(remote_control::Packet(remote_control::Command::WatchScreen));
        return packets;
    }

    QByteArray payload;
    QBuffer buffer(&payload);
    buffer.open(QIODevice::WriteOnly);
    screen->grabWindow(0).toImage().save(&buffer, "PNG");
    packets.append(remote_control::Packet(remote_control::Command::WatchScreen, payload));
    return packets;
}

QList<remote_control::Packet> CommandService::handleLockMachine() const
{
    this->m_lockWindow->lockMachine();
    return { statusPacket(remote_control::Command::LockMachine, true, tr("Lock machine completed.")) };
}

QList<remote_control::Packet> CommandService::handleUnlockMachine() const
{
    this->m_lockWindow->unlockMachine();
    return { statusPacket(remote_control::Command::UnlockMachine, true, tr("Unlock machine completed.")) };
}

QList<remote_control::Packet> CommandService::handleDeleteFile(const QByteArray& _payload) const
{
    const QString path = decodePath(_payload);
    QFileInfo info(path);
    if (!info.exists()) {
        return { statusPacket(remote_control::Command::DeleteFile, false, tr("Target does not exist: %1").arg(path)) };
    }

    bool success = false;
    if (info.isDir()) {
        success = QDir(path).removeRecursively();
    } else {
        success = QFile::remove(path);
    }

    if (!success) {
        return { statusPacket(remote_control::Command::DeleteFile, false, tr("Failed to delete target: %1").arg(path)) };
    }
    return { statusPacket(remote_control::Command::DeleteFile, true, tr("Delete completed.")) };
}

QList<remote_control::Packet> CommandService::handleTestConnection() const
{
    return { remote_control::Packet(remote_control::Command::TestConnection) };
}
