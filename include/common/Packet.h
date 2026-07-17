#pragma once

#include "common/Protocol.h"

#include <QByteArray>

#include <optional>

namespace remote_control
{

/** @brief Represents one framed request or response in the remote-control protocol. */
class Packet
{
public:
    /** @brief Byte-order-aware marker placed at the beginning of every packet. */
    static constexpr quint16 Header{0xFEFF};

    /** @brief Maximum accepted payload size in bytes. */
    static constexpr int MaximumPayloadSize{64 * 1024 * 1024};

    /** @brief Maximum serialized packet size in bytes. */
    static constexpr int MaximumSerializedSize{MaximumPayloadSize + 10};

    /** @brief Creates an empty connection-test packet. */
    Packet() = default;

    /** @brief Creates a packet for a command and optional payload. */
    Packet(Command _command, QByteArray _payload = {});

    /** @brief Calculates the payload checksum. */
    [[nodiscard]] quint16 checksum() const noexcept;

    /** @brief Serializes the packet using the wire format. */
    [[nodiscard]] QByteArray serialize() const;

    /** @brief Parses and removes one complete packet from a receive buffer when available. */
    [[nodiscard]] static std::optional<Packet> tryParse(QByteArray& _buffer);

    /** @brief Command encoded by this packet. */
    Command command{Command::TestConnection};

    /** @brief Command-specific payload bytes. */
    QByteArray payload;
};

}  // namespace remote_control
