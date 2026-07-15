#pragma once

#include "Protocol.h"

#include <QByteArray>

#include <optional>

namespace remote_control {

class Packet {
public:
    static constexpr quint16 Header = 0xFEFF;
    static constexpr int MaximumPayloadSize = 64 * 1024 * 1024;
    static constexpr int MaximumSerializedSize = MaximumPayloadSize + 10;

    Packet() = default;
    Packet(Command _command, QByteArray _payload = {});

    quint16 checksum() const;
    QByteArray serialize() const;

    static std::optional<Packet> tryParse(QByteArray& _buffer);

    Command command = Command::TestConnection;
    QByteArray payload;
};

}
