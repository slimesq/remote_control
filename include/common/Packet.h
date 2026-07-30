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
    static constexpr quint16 Header{0xFEFF};  ///< Marker at the beginning of every packet.

    static constexpr int MaximumPayloadSize{64 * 1024 * 1024};  ///< Maximum payload size in bytes.

    static constexpr int MaximumSerializedSize{MaximumPayloadSize + 10};  ///< Packet byte limit.

    /** @brief Creates an empty connection-test packet. */
    Packet() = default;

    /**
     * @brief Creates a packet for a command and optional payload.
     * @param _command Protocol command.
     * @param _payload Command-specific payload bytes.
     */
    Packet(Command _command, QByteArray _payload = {});

    /**
     * @brief Calculates the payload checksum.
     * @return 16-bit payload checksum.
     */
    [[nodiscard]] quint16 checksum() const noexcept;

    /**
     * @brief Serializes the packet using the wire format.
     * @return Serialized packet, or an empty array for an oversized payload.
     */
    [[nodiscard]] QByteArray serialize() const;

    /**
     * @brief Parses and removes one complete packet from a receive buffer when available.
     * @param _buffer Receive buffer updated as bytes are consumed.
     * @return Parsed packet, or std::nullopt when no complete valid packet is available.
     */
    [[nodiscard]] static std::optional<Packet> tryParse(QByteArray& _buffer);

    Command command{Command::TestConnection};  ///< Command encoded by this packet.
    QByteArray payload;                        ///< Command-specific payload bytes.
};

}  // namespace remote_control
