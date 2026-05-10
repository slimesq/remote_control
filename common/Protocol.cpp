#include "Protocol.h"

#include <QFileInfo>

#include <cstring>

namespace remoteqt {

QByteArray FileEntry::toPayload() const
{
    LegacyFileInfo info;
    info.isInvalid = isInvalid ? 1 : 0;
    info.isDirectory = isDirectory ? 1 : 0;
    info.hasNext = hasNext ? 1 : 0;

    const QByteArray rawName = encodeLocal8Bit(fileName);
    const int length = qMin(rawName.size(), static_cast<int>(sizeof(info.fileName) - 1));
    if (length > 0) {
        std::memcpy(info.fileName, rawName.constData(), static_cast<size_t>(length));
    }
    info.fileName[length] = '\0';

    return QByteArray(reinterpret_cast<const char*>(&info), static_cast<int>(sizeof(info)));
}

FileEntry FileEntry::fromPayload(const QByteArray& payload)
{
    FileEntry entry;
    if (payload.size() < static_cast<int>(sizeof(LegacyFileInfo))) {
        entry.isInvalid = true;
        entry.hasNext = false;
        return entry;
    }

    LegacyFileInfo info;
    std::memcpy(&info, payload.constData(), sizeof(info));
    entry.isInvalid = info.isInvalid != 0;
    entry.isDirectory = info.isDirectory != 0;
    entry.hasNext = info.hasNext != 0;
    entry.fileName = decodeLocal8Bit(QByteArray(info.fileName, qstrnlen(info.fileName, static_cast<int>(sizeof(info.fileName)))));
    return entry;
}

QByteArray makeStatusPayload(bool success, const QString& message)
{
    QByteArray payload;
    payload.append(success ? '\x01' : '\x00');
    payload.append(encodeLocal8Bit(message));
    return payload;
}

bool parseStatusPayload(const QByteArray& payload, bool defaultSuccess, QString* messageOut)
{
    if (payload.isEmpty()) {
        if (messageOut) {
            messageOut->clear();
        }
        return defaultSuccess;
    }

    const bool success = payload.front() != '\0';
    if (messageOut) {
        *messageOut = decodeLocal8Bit(payload.mid(1));
    }
    return success;
}

QString decodeLocal8Bit(const QByteArray& data)
{
    return QString::fromLocal8Bit(data);
}

QByteArray encodeLocal8Bit(const QString& text)
{
    return text.toLocal8Bit();
}

}
