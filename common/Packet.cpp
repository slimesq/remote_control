#include "Packet.h"

#include <QDataStream>
#include <QIODevice>

namespace remoteqt {

namespace {

quint16 readUInt16LE(const QByteArray& bytes, int offset)
{
    return static_cast<quint16>(static_cast<unsigned char>(bytes[offset]))
        | (static_cast<quint16>(static_cast<unsigned char>(bytes[offset + 1])) << 8);
}

quint32 readUInt32LE(const QByteArray& bytes, int offset)
{
    return static_cast<quint32>(static_cast<unsigned char>(bytes[offset]))
        | (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 1])) << 8)
        | (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 2])) << 16)
        | (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
}

}

Packet::Packet(Command packetCommand, QByteArray packetPayload)
    : command(packetCommand)
    , payload(std::move(packetPayload))
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

std::optional<Packet> Packet::tryParse(QByteArray& buffer)
{
    static const QByteArray headerBytes("\xFF\xFE", 2);

    while (true) {
        const int headerIndex = buffer.indexOf(headerBytes);
        if (headerIndex < 0) {
            buffer.clear();
            return std::nullopt;
        }
        if (headerIndex > 0) {
            buffer.remove(0, headerIndex);
        }

        if (buffer.size() < 10) {
            return std::nullopt;
        }

        const quint32 length = readUInt32LE(buffer, 2);
        const int packetSize = 2 + 4 + static_cast<int>(length);
        if (buffer.size() < packetSize) {
            return std::nullopt;
        }

        const quint16 command = readUInt16LE(buffer, 6);
        const int payloadSize = static_cast<int>(length) - 4;
        QByteArray payload;
        if (payloadSize > 0) {
            payload = buffer.mid(8, payloadSize);
        }
        const quint16 expectedSum = readUInt16LE(buffer, 8 + payloadSize);

        Packet packet(static_cast<Command>(command), payload);
        if (packet.checksum() != expectedSum) {
            buffer.remove(0, 2);
            continue;
        }

        buffer.remove(0, packetSize);
        return packet;
    }
}

}
