#pragma once

#include "Protocol.h"

#include <QByteArray>

#include <optional>

namespace remoteqt {

class Packet {
public:
    static constexpr quint16 Header = 0xFEFF;

    Packet() = default;
    Packet(Command command, QByteArray payload = {});

    quint16 checksum() const;
    QByteArray serialize() const;

    static std::optional<Packet> tryParse(QByteArray& buffer);

    Command command = Command::TestConnection;
    QByteArray payload;
};

}
