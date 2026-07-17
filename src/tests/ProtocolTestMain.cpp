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

/**
 * @brief Records whether a protocol-test condition passed.
 * @param _condition Condition to verify.
 * @param _message Failure message printed when the condition is false.
 * @return The supplied condition.
 */
bool expect(bool _condition, char const* _message)
{
    if (!_condition)
    {
        std::cerr << "FAILED: " << _message << std::endl;
    }
    return _condition;
}

/**
 * @brief Creates a packet header with the requested length field.
 * @param _length Packet length field to encode.
 * @return Serialized packet header.
 */
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

/**
 * @brief Verifies recovery from malformed packet lengths.
 * @return true when all malformed-length cases pass; otherwise false.
 */
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

/**
 * @brief Verifies parsing when TCP splits the two-byte packet header.
 * @return true when the partial header is retained and the packet is reconstructed.
 */
bool testSplitPacketHeader()
{
    remote_control::Packet const expectedPacket{remote_control::Command::TestConnection,
                                                QByteArrayLiteral("split-header")};
    QByteArray const serialized{expectedPacket.serialize()};
    QByteArray buffer{QByteArrayLiteral("noise\xFF")};

    bool passed{true};
    auto const incompletePacket{remote_control::Packet::tryParse(buffer)};
    passed &= expect(!incompletePacket.has_value(), "split header should remain incomplete");
    passed &= expect(buffer == QByteArray{"\xFF", 1},
                     "parser should retain the first byte of a split header");

    buffer.append(serialized.mid(1));
    auto const parsedPacket{remote_control::Packet::tryParse(buffer)};
    passed &= expect(parsedPacket.has_value(), "split header packet should be reconstructed");
    if (parsedPacket.has_value())
    {
        passed &= expect(parsedPacket->command == expectedPacket.command,
                         "split header command should match");
        passed &= expect(parsedPacket->payload == expectedPacket.payload,
                         "split header payload should match");
    }
    return passed;
}

/**
 * @brief Verifies UTF-8 file-entry serialization.
 * @return true when the round-trip test passes; otherwise false.
 */
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

/**
 * @brief Verifies rejection of truncated file-entry data.
 * @return true when truncated data is rejected; otherwise false.
 */
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

/**
 * @brief Verifies UTF-8 status-message serialization.
 * @return true when the round-trip test passes; otherwise false.
 */
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

/**
 * @brief Runs the protocol test suite.
 * @return EXIT_SUCCESS when all tests pass; otherwise EXIT_FAILURE.
 */
int main()
{
    bool passed{true};
    passed &= testMalformedPacketLengths();
    passed &= testSplitPacketHeader();
    passed &= testUtf8FileEntryRoundTrip();
    passed &= testInvalidFileEntryPayload();
    passed &= testUtf8StatusRoundTrip();

    std::cout << (passed ? "PROTOCOL TESTS PASSED" : "PROTOCOL TESTS FAILED") << std::endl;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
