#include "Packet.h"

#include <QDataStream>
#include <QIODevice>

namespace remote_control {

namespace {

quint16 readUInt16LE(const QByteArray& _bytes, int _offset)
{
    return static_cast<quint16>(static_cast<unsigned char>(_bytes[_offset]))
        | (static_cast<quint16>(static_cast<unsigned char>(_bytes[_offset + 1])) << 8);
}

quint32 readUInt32LE(const QByteArray& _bytes, int _offset)
{
    return static_cast<quint32>(static_cast<unsigned char>(_bytes[_offset]))
        | (static_cast<quint32>(static_cast<unsigned char>(_bytes[_offset + 1])) << 8)
        | (static_cast<quint32>(static_cast<unsigned char>(_bytes[_offset + 2])) << 16)
        | (static_cast<quint32>(static_cast<unsigned char>(_bytes[_offset + 3])) << 24);
}

}

Packet::Packet(Command _packetCommand, QByteArray _packetPayload)
    : command(_packetCommand)
    , payload(std::move(_packetPayload))
{
}

quint16 Packet::checksum() const
{
    quint16 sum = 0;
    for (const char byte : payload) {
        sum = static_cast<quint16>(sum + static_cast<unsigned char>(byte));
    }
    return sum;
}

QByteArray Packet::serialize() const
{
    if (payload.size() > MaximumPayloadSize) {
        return {};
    }

    QByteArray bytes;
    bytes.resize(2 + 4 + 2 + payload.size() + 2);

    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << Header;
    stream << static_cast<quint32>(payload.size() + 4);
    stream << static_cast<quint16>(command);
    if (!payload.isEmpty()) {
        stream.writeRawData(payload.constData(), payload.size());
    }
    stream << checksum();
    return bytes;
}

std::optional<Packet> Packet::tryParse(QByteArray& _buffer)
{
    static const QByteArray headerBytes("\xFF\xFE", 2);

    while (true) {
        const int headerIndex = _buffer.indexOf(headerBytes);
        if (headerIndex < 0) {
            _buffer.clear();
            return std::nullopt;
        }
        if (headerIndex > 0) {
            _buffer.remove(0, headerIndex);
        }

        if (_buffer.size() < 10) {
            return std::nullopt;
        }

        const quint32 length = readUInt32LE(_buffer, 2);
        constexpr quint32 minimumLength = 4;
        constexpr quint32 maximumLength = static_cast<quint32>(MaximumPayloadSize) + minimumLength;
        if (length < minimumLength || length > maximumLength) {
            // Drop the invalid header and continue scanning for the next packet.
            _buffer.remove(0, 2);
            continue;
        }

        const qint64 packetSize64 = 2LL + 4LL + static_cast<qint64>(length);
        if (packetSize64 > _buffer.size()) {
            return std::nullopt;
        }
        const int packetSize = static_cast<int>(packetSize64);

        const quint16 command = readUInt16LE(_buffer, 6);
        const int payloadSize = static_cast<int>(length) - 4;
        QByteArray payload;
        if (payloadSize > 0) {
            payload = _buffer.mid(8, payloadSize);
        }
        const quint16 expectedSum = readUInt16LE(_buffer, 8 + payloadSize);

        Packet packet(static_cast<Command>(command), payload);
        if (packet.checksum() != expectedSum) {
            _buffer.remove(0, 2);
            continue;
        }

        _buffer.remove(0, packetSize);
        return packet;
    }
}

}
