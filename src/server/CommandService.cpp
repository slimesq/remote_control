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

QByteArray sizeToPayload(qint64 size)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << size;
    return payload;
}

remoteqt::Packet statusPacket(remoteqt::Command command, bool success, const QString& message = {})
{
    return remoteqt::Packet(command, remoteqt::makeStatusPayload(success, message));
}

QString decodePath(const QByteArray& payload)
{
    return remoteqt::decodeLocal8Bit(payload);
}

DWORD mouseDownFlag(remoteqt::MouseButton button)
{
    switch (button) {
    case remoteqt::MouseButton::Left:
        return MOUSEEVENTF_LEFTDOWN;
    case remoteqt::MouseButton::Right:
        return MOUSEEVENTF_RIGHTDOWN;
    case remoteqt::MouseButton::Middle:
        return MOUSEEVENTF_MIDDLEDOWN;
    default:
        return 0;
    }
}

DWORD mouseUpFlag(remoteqt::MouseButton button)
{
    switch (button) {
    case remoteqt::MouseButton::Left:
        return MOUSEEVENTF_LEFTUP;
    case remoteqt::MouseButton::Right:
        return MOUSEEVENTF_RIGHTUP;
    case remoteqt::MouseButton::Middle:
        return MOUSEEVENTF_MIDDLEUP;
    default:
        return 0;
    }
}

void sendMouseFlag(DWORD flag)
{
    if (flag == 0) {
        return;
    }
    INPUT input {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    SendInput(1, &input, sizeof(INPUT));
}

}

CommandService::CommandService(QObject* parent)
    : QObject(parent)
    , m_lockWindow(std::make_unique<LockWindow>())
{
}

CommandService::~CommandService()
{
    unlockLocalMachine();
}

void CommandService::lockLocalMachine()
{
    m_lockWindow->lockMachine();
}

void CommandService::unlockLocalMachine()
{
    m_lockWindow->unlockMachine();
}

bool CommandService::isLocked() const
{
    return m_lockWindow->isLocked();
}

QList<remoteqt::Packet> CommandService::handle(const remoteqt::Packet& request)
{
    switch (request.command) {
    case remoteqt::Command::ListDrives:
        return handleListDrives();
    case remoteqt::Command::ListDirectory:
        return handleListDirectory(request.payload);
    case remoteqt::Command::RunFile:
        return handleRunFile(request.payload);
    case remoteqt::Command::DownloadFile:
        return handleDownloadFile(request.payload);
    case remoteqt::Command::MouseEvent:
        return handleMouseEvent(request.payload);
    case remoteqt::Command::WatchScreen:
        return handleWatchScreen();
    case remoteqt::Command::LockMachine:
        return handleLockMachine();
    case remoteqt::Command::UnlockMachine:
        return handleUnlockMachine();
    case remoteqt::Command::DeleteFile:
        return handleDeleteFile(request.payload);
    case remoteqt::Command::TestConnection:
        return handleTestConnection();
    }
    return {};
}

QList<remoteqt::Packet> CommandService::handleListDrives() const
{
    QStringList drives;
    const DWORD mask = GetLogicalDrives();
    for (int index = 0; index < 26; ++index) {
        if ((mask & (1u << index)) != 0) {
            drives << QString(QChar('A' + index));
        }
    }
    return { remoteqt::Packet(remoteqt::Command::ListDrives, remoteqt::encodeLocal8Bit(drives.join(','))) };
}

QList<remoteqt::Packet> CommandService::handleListDirectory(const QByteArray& payload) const
{
    QList<remoteqt::Packet> packets;
    const QString path = decodePath(payload);
    QFileInfo dirInfo(path);
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        remoteqt::FileEntry entry;
        entry.isInvalid = true;
        entry.hasNext = false;
        packets.append(remoteqt::Packet(remoteqt::Command::ListDirectory, entry.toPayload()));
        return packets;
    }

    QDir dir(path);
    if (!dir.isReadable()) {
        remoteqt::FileEntry entry;
        entry.isInvalid = true;
        entry.hasNext = false;
        packets.append(remoteqt::Packet(remoteqt::Command::ListDirectory, entry.toPayload()));
        return packets;
    }

    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::DirsFirst | QDir::Name);

    for (const QFileInfo& info : entries) {
        remoteqt::FileEntry entry;
        entry.isDirectory = info.isDir();
        entry.fileName = info.fileName();
        packets.append(remoteqt::Packet(remoteqt::Command::ListDirectory, entry.toPayload()));
    }

    // A final marker packet tells the client that the streamed listing is complete.
    remoteqt::FileEntry endEntry;
    endEntry.hasNext = false;
    packets.append(remoteqt::Packet(remoteqt::Command::ListDirectory, endEntry.toPayload()));
    return packets;
}

QList<remoteqt::Packet> CommandService::handleRunFile(const QByteArray& payload) const
{
    const QString path = decodePath(payload);
    const HINSTANCE result = ShellExecuteW(nullptr, nullptr, reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        return { statusPacket(remoteqt::Command::RunFile, false, tr("Failed to open file: %1").arg(path)) };
    }
    return { statusPacket(remoteqt::Command::RunFile, true, tr("Open file completed.")) };
}

QList<remoteqt::Packet> CommandService::handleDownloadFile(const QByteArray& payload) const
{
    QList<remoteqt::Packet> packets;
    QFile file(decodePath(payload));
    if (!file.open(QIODevice::ReadOnly)) {
        packets.append(remoteqt::Packet(remoteqt::Command::DownloadFile, sizeToPayload(-1)));
        return packets;
    }

    // Downloads start with a size header so the client can pre-validate and track progress.
    packets.append(remoteqt::Packet(remoteqt::Command::DownloadFile, sizeToPayload(file.size())));
    while (!file.atEnd()) {
        packets.append(remoteqt::Packet(remoteqt::Command::DownloadFile, file.read(1024)));
    }
    return packets;
}

QList<remoteqt::Packet> CommandService::handleMouseEvent(const QByteArray& payload) const
{
    if (payload.size() >= static_cast<int>(sizeof(remoteqt::MouseEventPacket))) {
        remoteqt::MouseEventPacket event {};
        std::memcpy(&event, payload.constData(), sizeof(event));
        SetCursorPos(event.x, event.y);

        const auto button = static_cast<remoteqt::MouseButton>(event.button);
        const auto action = static_cast<remoteqt::MouseAction>(event.action);

        switch (action) {
        case remoteqt::MouseAction::Click:
            if (button != remoteqt::MouseButton::None) {
                sendMouseFlag(mouseDownFlag(button));
                sendMouseFlag(mouseUpFlag(button));
            }
            break;
        case remoteqt::MouseAction::DoubleClick:
            if (button != remoteqt::MouseButton::None) {
                sendMouseFlag(mouseDownFlag(button));
                sendMouseFlag(mouseUpFlag(button));
                sendMouseFlag(mouseDownFlag(button));
                sendMouseFlag(mouseUpFlag(button));
            }
            break;
        case remoteqt::MouseAction::Press:
            sendMouseFlag(mouseDownFlag(button));
            break;
        case remoteqt::MouseAction::Release:
            sendMouseFlag(mouseUpFlag(button));
            break;
        }
    }
    return { statusPacket(remoteqt::Command::MouseEvent, true) };
}

QList<remoteqt::Packet> CommandService::handleWatchScreen() const
{
    QList<remoteqt::Packet> packets;
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        packets.append(remoteqt::Packet(remoteqt::Command::WatchScreen));
        return packets;
    }

    QByteArray payload;
    QBuffer buffer(&payload);
    buffer.open(QIODevice::WriteOnly);
    screen->grabWindow(0).toImage().save(&buffer, "PNG");
    packets.append(remoteqt::Packet(remoteqt::Command::WatchScreen, payload));
    return packets;
}

QList<remoteqt::Packet> CommandService::handleLockMachine() const
{
    m_lockWindow->lockMachine();
    return { statusPacket(remoteqt::Command::LockMachine, true, tr("Lock machine completed.")) };
}

QList<remoteqt::Packet> CommandService::handleUnlockMachine() const
{
    m_lockWindow->unlockMachine();
    return { statusPacket(remoteqt::Command::UnlockMachine, true, tr("Unlock machine completed.")) };
}

QList<remoteqt::Packet> CommandService::handleDeleteFile(const QByteArray& payload) const
{
    const QString path = decodePath(payload);
    QFileInfo info(path);
    if (!info.exists()) {
        return { statusPacket(remoteqt::Command::DeleteFile, false, tr("Target does not exist: %1").arg(path)) };
    }

    bool success = false;
    if (info.isDir()) {
        success = QDir(path).removeRecursively();
    } else {
        success = QFile::remove(path);
    }

    if (!success) {
        return { statusPacket(remoteqt::Command::DeleteFile, false, tr("Failed to delete target: %1").arg(path)) };
    }
    return { statusPacket(remoteqt::Command::DeleteFile, true, tr("Delete completed.")) };
}

QList<remoteqt::Packet> CommandService::handleTestConnection() const
{
    return { remoteqt::Packet(remoteqt::Command::TestConnection) };
}
