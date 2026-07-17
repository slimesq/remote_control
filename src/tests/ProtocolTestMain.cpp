#include "common/Packet.h"
#include "common/Protocol.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QString>

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{

constexpr int LongFileNameRepeatCount{100};
constexpr ushort UnicodeCharacterCode{0x6587};
constexpr int MinimumLongPayloadSize{256};

bool expect(bool _condition, char const* _message)
{
    if (!_condition)
    {
        std::cerr << "FAILED: " << _message << std::endl;
    }
    return _condition;
}

QByteArray makePacketHeader(quint32 _length)
{
    QByteArray bytes;
    QDataStream stream{&bytes, QIODevice::WriteOnly};
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << remote_control::Packet::Header;
    stream << _length;
    stream << static_cast<quint16>(remote_control::Command::TestConnection);
    stream << static_cast<quint16>(0);
    return bytes;
}

bool testMalformedPacketLengths()
{
    remote_control::Packet const expectedPacket{remote_control::Command::TestConnection,
                                                QByteArrayLiteral("valid")};
    std::array<quint32, 5> const invalidLengths{
        0U,
        3U,
        static_cast<quint32>(remote_control::Packet::MaximumPayloadSize) + 5U,
        0x80000000U,
        0xFFFFFFFFU};

    bool passed{true};
    for (quint32 const invalidLength : invalidLengths)
    {
        QByteArray buffer{makePacketHeader(invalidLength)};
        buffer.append(expectedPacket.serialize());

        auto const parsedPacket{remote_control::Packet::tryParse(buffer)};
        passed &= expect(parsedPacket.has_value(), "parser should recover after an invalid length");
        if (parsedPacket.has_value())
        {
            passed &= expect(parsedPacket->command == expectedPacket.command,
                             "recovered packet command should match");
            passed &= expect(parsedPacket->payload == expectedPacket.payload,
                             "recovered packet payload should match");
        }
    }
    return passed;
}

bool testUtf8FileEntryRoundTrip()
{
    remote_control::FileEntry expectedEntry;
    expectedEntry.isDirectory = true;
    expectedEntry.hasNext = true;
    expectedEntry.fileName = QStringLiteral("目录_") +
        QString{LongFileNameRepeatCount, QChar{UnicodeCharacterCode}} + QStringLiteral("_файл.txt");

    QByteArray const payload{expectedEntry.toPayload()};
    remote_control::FileEntry const parsedEntry{remote_control::FileEntry::fromPayload(payload)};

    bool passed{true};
    passed &= expect(payload.size() > MinimumLongPayloadSize,
                     "long UTF-8 file name should not be truncated");
    passed &= expect(!parsedEntry.isInvalid, "valid file entry should remain valid");
    passed &= expect(parsedEntry.isDirectory == expectedEntry.isDirectory,
                     "directory flag should round-trip");
    passed &=
        expect(parsedEntry.hasNext == expectedEntry.hasNext, "continuation flag should round-trip");
    passed &= expect(parsedEntry.fileName == expectedEntry.fileName,
                     "UTF-8 file name should round-trip without loss");
    return passed;
}

bool testInvalidFileEntryPayload()
{
    remote_control::FileEntry entry;
    entry.fileName = QStringLiteral("测试.txt");
    QByteArray payload{entry.toPayload()};
    payload.chop(1);

    remote_control::FileEntry const parsedEntry{remote_control::FileEntry::fromPayload(payload)};
    return expect(parsedEntry.isInvalid && !parsedEntry.hasNext,
                  "truncated file entry should be rejected");
}

bool testUtf8StatusRoundTrip()
{
    QString const expectedMessage{QStringLiteral("操作失败：文件不存在")};
    QByteArray const payload{remote_control::makeStatusPayload(false, expectedMessage)};
    QString parsedMessage;
    bool const success{remote_control::parseStatusPayload(payload, true, &parsedMessage)};
    return expect(!success && parsedMessage == expectedMessage,
                  "UTF-8 status message should round-trip");
}

}  // namespace

int main()
{
    bool passed{true};
    passed &= testMalformedPacketLengths();
    passed &= testUtf8FileEntryRoundTrip();
    passed &= testInvalidFileEntryPayload();
    passed &= testUtf8StatusRoundTrip();

    std::cout << (passed ? "PROTOCOL TESTS PASSED" : "PROTOCOL TESTS FAILED") << std::endl;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
