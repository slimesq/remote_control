#include "common/Packet.h"

#include <QDataStream>
#include <QIODevice>

namespace remote_control
{

namespace
{

constexpr int BitsPerByte{8};
constexpr int MinimumSerializedSize{10};
constexpr int PayloadOffset{8};
constexpr quint32 MinimumLength{4};
constexpr quint32 MaximumLength{static_cast<quint32>(Packet::MaximumPayloadSize) + MinimumLength};

/**
 * @brief Reads a little-endian 16-bit value from a byte array.
 * @param _bytes Source byte array.
 * @param _offset Byte offset of the value.
 * @return Decoded 16-bit value.
 */
quint16 readUInt16LE(QByteArray const& _bytes, int _offset)
{
    return static_cast<quint16>(static_cast<unsigned char>(_bytes[_offset])) |
        (static_cast<quint16>(static_cast<unsigned char>(_bytes[_offset + 1])) << BitsPerByte);
}

/**
 * @brief Reads a little-endian 32-bit value from a byte array.
 * @param _bytes Source byte array.
 * @param _offset Byte offset of the value.
 * @return Decoded 32-bit value.
 */
quint32 readUInt32LE(QByteArray const& _bytes, int _offset)
{
    return static_cast<quint32>(static_cast<unsigned char>(_bytes[_offset])) |
        (static_cast<quint32>(static_cast<unsigned char>(_bytes[_offset + 1])) << BitsPerByte) |
        (static_cast<quint32>(static_cast<unsigned char>(_bytes[_offset + 2]))
         << (BitsPerByte * 2)) |
        (static_cast<quint32>(static_cast<unsigned char>(_bytes[_offset + 3]))
         << (BitsPerByte * 3));
}

}  // namespace

Packet::Packet(Command _command, QByteArray _payload)
    : command{_command}, payload{std::move(_payload)}
{
}

quint16 Packet::checksum() const noexcept
{
    quint16 sum{0};
    for (char const byte : payload)
    {
        sum = static_cast<quint16>(sum + static_cast<unsigned char>(byte));
    }
    return sum;
}

QByteArray Packet::serialize() const
{
    if (payload.size() > MaximumPayloadSize)
    {
        return {};
    }

    QByteArray bytes;
    bytes.resize(2 + 4 + 2 + payload.size() + 2);

    QDataStream stream{&bytes, QIODevice::WriteOnly};
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << Header;
    stream << static_cast<quint32>(payload.size() + 4);
    stream << static_cast<quint16>(command);
    if (!payload.isEmpty())
    {
        stream.writeRawData(payload.constData(), payload.size());
    }
    stream << checksum();
    return bytes;
}

std::optional<Packet> Packet::tryParse(QByteArray& _buffer)
{
    static QByteArray const headerBytes{"\xFF\xFE", 2};

    while (true)
    {
        // 1. Resynchronize the receive buffer to the next protocol header.
        qsizetype const headerIndex{_buffer.indexOf(headerBytes)};
        if (headerIndex < 0)
        {
            // Preserve a trailing first-header byte because TCP may split FF FE between reads.
            bool const hasPartialHeader{!_buffer.isEmpty() &&
                                        static_cast<unsigned char>(_buffer.back()) == 0xFFU};
            _buffer = hasPartialHeader ? QByteArray{"\xFF", 1} : QByteArray{};
            return std::nullopt;
        }
        if (headerIndex > 0)
        {
            _buffer.remove(0, headerIndex);
        }

        if (_buffer.size() < MinimumSerializedSize)
        {
            return std::nullopt;
        }

        // 2. Validate the declared length before trusting frame offsets.
        quint32 const length{readUInt32LE(_buffer, 2)};
        if (length < MinimumLength || length > MaximumLength)
        {
            _buffer.remove(0, 2);
            continue;
        }

        // 3. Leave a partial frame buffered until the remaining bytes arrive.
        qint64 const packetSize64{2LL + 4LL + static_cast<qint64>(length)};
        if (packetSize64 > _buffer.size())
        {
            return std::nullopt;
        }
        int const packetSize{static_cast<int>(packetSize64)};

        quint16 const command{readUInt16LE(_buffer, 6)};
        int const payloadSize{static_cast<int>(length - MinimumLength)};
        QByteArray payload;
        if (payloadSize > 0)
        {
            payload = _buffer.mid(PayloadOffset, payloadSize);
        }
        quint16 const expectedSum{readUInt16LE(_buffer, PayloadOffset + payloadSize)};

        // 4. Consume the frame only after its checksum has been verified.
        Packet const packet{static_cast<Command>(command), payload};
        if (packet.checksum() != expectedSum)
        {
            _buffer.remove(0, 2);
            continue;
        }

        _buffer.remove(0, packetSize);
        return packet;
    }
}

}  // namespace remote_control
