#include "common/Protocol.h"

#include <QDataStream>
#include <QIODevice>

namespace remote_control
{

namespace
{

constexpr quint8 FileEntryPayloadVersion{1};
constexpr quint8 InvalidFlag{1U << 0U};
constexpr quint8 DirectoryFlag{1U << 1U};
constexpr quint8 HasNextFlag{1U << 2U};
constexpr quint8 KnownFlags{InvalidFlag | DirectoryFlag | HasNextFlag};
constexpr int FileEntryHeaderSize{
    static_cast<int>(sizeof(quint8) + sizeof(quint8) + sizeof(quint32))};

/**
 * @brief Creates a terminating invalid file entry.
 * @return Invalid entry with no following packet.
 */
FileEntry invalidFileEntry()
{
    FileEntry entry;
    entry.isInvalid = true;
    entry.hasNext = false;
    return entry;
}

}  // namespace

QByteArray FileEntry::toPayload() const
{
    quint8 flags{0};
    if (isInvalid)
    {
        flags |= InvalidFlag;
    }
    if (isDirectory)
    {
        flags |= DirectoryFlag;
    }
    if (hasNext)
    {
        flags |= HasNextFlag;
    }

    QByteArray const rawName{encodeUtf8(fileName)};
    QByteArray payload;
    QDataStream stream{&payload, QIODevice::WriteOnly};
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << FileEntryPayloadVersion;
    stream << flags;
    stream << static_cast<quint32>(rawName.size());
    if (!rawName.isEmpty())
    {
        stream.writeRawData(rawName.constData(), rawName.size());
    }
    return payload;
}

FileEntry FileEntry::fromPayload(QByteArray const& _payload)
{
    if (_payload.size() < FileEntryHeaderSize)
    {
        return invalidFileEntry();
    }

    QDataStream stream{_payload};
    stream.setByteOrder(QDataStream::LittleEndian);
    quint8 version{0};
    quint8 flags{0};
    quint32 nameLength{0};
    stream >> version;
    stream >> flags;
    stream >> nameLength;

    int const remainingBytes{_payload.size() - FileEntryHeaderSize};
    if (stream.status() != QDataStream::Ok || version != FileEntryPayloadVersion ||
        (flags & static_cast<quint8>(~KnownFlags)) != 0 ||
        nameLength != static_cast<quint32>(remainingBytes))
    {
        return invalidFileEntry();
    }

    QByteArray const rawName{_payload.mid(FileEntryHeaderSize, remainingBytes)};
    QString const fileName{decodeUtf8(rawName)};
    if (encodeUtf8(fileName) != rawName)
    {
        return invalidFileEntry();
    }

    FileEntry entry;
    entry.isInvalid = (flags & InvalidFlag) != 0;
    entry.isDirectory = (flags & DirectoryFlag) != 0;
    entry.hasNext = (flags & HasNextFlag) != 0;
    entry.fileName = fileName;
    return entry;
}

QByteArray makeStatusPayload(bool _success, QString const& _message)
{
    QByteArray payload;
    payload.append(_success ? '\x01' : '\x00');
    payload.append(encodeUtf8(_message));
    return payload;
}

bool parseStatusPayload(QByteArray const& _payload, bool _defaultSuccess, QString* _messageOut)
{
    if (_payload.isEmpty())
    {
        if (_messageOut)
        {
            _messageOut->clear();
        }
        return _defaultSuccess;
    }

    bool const success{_payload.front() != '\0'};
    if (_messageOut)
    {
        *_messageOut = decodeUtf8(_payload.mid(1));
    }
    return success;
}

QString decodeUtf8(QByteArray const& _data)
{
    return QString::fromUtf8(_data);
}

QByteArray encodeUtf8(QString const& _text)
{
    return _text.toUtf8();
}

}  // namespace remote_control
