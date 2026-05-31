#include "Packet.h"
#include "Protocol.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <functional>
#include <iostream>

#include <windows.h>

#ifdef DeleteFile
#undef DeleteFile
#endif

namespace {

struct ResponseBundle {
    QList<remoteqt::Packet> packets;
    QString error;
};

ResponseBundle sendCommand(
    const QString& host,
    quint16 port,
    remoteqt::Command command,
    const QByteArray& payload = {})
{
    ResponseBundle result;
    QTcpSocket socket;
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(5000)) {
        result.error = socket.errorString();
        return result;
    }

    socket.write(remoteqt::Packet(command, payload).serialize());
    if (!socket.waitForBytesWritten(5000)) {
        result.error = socket.errorString();
        return result;
    }

    QByteArray buffer;
    while (socket.state() != QAbstractSocket::UnconnectedState) {
        if (!socket.waitForReadyRead(5000)) {
            if (socket.state() == QAbstractSocket::UnconnectedState) {
                break;
            }
            result.error = socket.errorString().isEmpty() ? QStringLiteral("Timed out waiting for response") : socket.errorString();
            return result;
        }
        buffer.append(socket.readAll());
        while (true) {
            auto packet = remoteqt::Packet::tryParse(buffer);
            if (!packet.has_value()) {
                break;
            }
            result.packets.push_back(packet.value());
        }
    }

    if (!buffer.isEmpty()) {
        while (true) {
            auto packet = remoteqt::Packet::tryParse(buffer);
            if (!packet.has_value()) {
                break;
            }
            result.packets.push_back(packet.value());
        }
    }

    return result;
}

bool expect(bool condition, const QString& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message.toStdString() << std::endl;
        return false;
    }
    std::cout << "[ OK ] " << message.toStdString() << std::endl;
    return true;
}

QString pickDrive(const QList<remoteqt::Packet>& packets)
{
    if (packets.isEmpty()) {
        return {};
    }
    const QStringList drives = remoteqt::decodeLocal8Bit(packets.first().payload).split(',', Qt::SkipEmptyParts);
    if (drives.isEmpty()) {
        return {};
    }
    QString drive = drives.first().trimmed();
    if (!drive.endsWith(':')) {
        drive.append(':');
    }
    return drive + '\\';
}

bool validateDirectoryResponse(const QList<remoteqt::Packet>& packets)
{
    if (packets.isEmpty()) {
        return false;
    }
    bool sawEnd = false;
    for (const remoteqt::Packet& packet : packets) {
        if (packet.command != remoteqt::Command::ListDirectory) {
            return false;
        }
        const remoteqt::FileEntry entry = remoteqt::FileEntry::fromPayload(packet.payload);
        if (!entry.hasNext) {
            sawEnd = true;
        }
    }
    return sawEnd;
}

bool validateDownload(
    const QString& host,
    quint16 port,
    const QString& remotePath,
    const QByteArray& expectedContent)
{
    const ResponseBundle response = sendCommand(host, port, remoteqt::Command::DownloadFile, remoteqt::encodeLocal8Bit(remotePath));
    if (!expect(response.error.isEmpty(), QStringLiteral("download request should succeed"))) {
        return false;
    }
    if (!expect(!response.packets.isEmpty(), QStringLiteral("download should return packets"))) {
        return false;
    }
    if (!expect(response.packets.first().payload.size() == static_cast<int>(sizeof(qint64)), QStringLiteral("download should start with length header"))) {
        return false;
    }

    qint64 expectedSize = -1;
    {
        QDataStream stream(response.packets.first().payload);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream >> expectedSize;
    }
    if (!expect(expectedSize == expectedContent.size(), QStringLiteral("download length should match source file"))) {
        return false;
    }

    QByteArray downloaded;
    for (int index = 1; index < response.packets.size(); ++index) {
        downloaded.append(response.packets[index].payload);
    }
    return expect(downloaded == expectedContent, QStringLiteral("downloaded content should match source file"));
}

bool validateDownloadFailure(
    const QString& host,
    quint16 port,
    const QString& remotePath)
{
    const ResponseBundle response = sendCommand(host, port, remoteqt::Command::DownloadFile, remoteqt::encodeLocal8Bit(remotePath));
    if (!expect(response.error.isEmpty(), QStringLiteral("download failure request should still return a response"))) {
        return false;
    }
    if (!expect(response.packets.size() == 1, QStringLiteral("failed download should return one header packet"))) {
        return false;
    }

    qint64 encodedSize = 0;
    QDataStream stream(response.packets.first().payload);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream >> encodedSize;
    return expect(encodedSize < 0, QStringLiteral("failed download should return a negative length marker"));
}

bool directoryContainsNames(const QList<remoteqt::Packet>& packets, const QStringList& names)
{
    QStringList actualNames;
    for (const remoteqt::Packet& packet : packets) {
        const remoteqt::FileEntry entry = remoteqt::FileEntry::fromPayload(packet.payload);
        if (entry.hasNext && !entry.isInvalid) {
            actualNames << entry.fileName;
        }
    }

    for (const QString& name : names) {
        if (!actualNames.contains(name)) {
            return false;
        }
    }
    return true;
}

bool validateStatusReply(
    const ResponseBundle& response,
    remoteqt::Command command,
    bool expectedSuccess,
    const QString& label)
{
    if (!expect(response.error.isEmpty(), label + QStringLiteral(": network request should succeed"))) {
        return false;
    }
    if (!expect(response.packets.size() == 1, label + QStringLiteral(": should return one packet"))) {
        return false;
    }
    if (!expect(!response.packets.isEmpty() && response.packets.first().command == command, label + QStringLiteral(": should echo the command"))) {
        return false;
    }
    QString message;
    const bool success = remoteqt::parseStatusPayload(response.packets.first().payload, true, &message);
    return expect(success == expectedSuccess, label + QStringLiteral(": status payload should match expected result"));
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QString host = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("127.0.0.1");
    const quint16 port = argc > 2 ? QString::fromLocal8Bit(argv[2]).toUShort() : 9527;

    bool allPassed = true;

    const ResponseBundle testResponse = sendCommand(host, port, remoteqt::Command::TestConnection);
    allPassed &= expect(testResponse.error.isEmpty(), QStringLiteral("test connection request should succeed"));
    allPassed &= expect(testResponse.packets.size() == 1, QStringLiteral("test connection should return one packet"));
    allPassed &= expect(!testResponse.packets.isEmpty() && testResponse.packets.first().command == remoteqt::Command::TestConnection,
        QStringLiteral("test connection should echo command 1981"));

    const ResponseBundle drivesResponse = sendCommand(host, port, remoteqt::Command::ListDrives);
    allPassed &= expect(drivesResponse.error.isEmpty(), QStringLiteral("list drives request should succeed"));
    const QString driveRoot = pickDrive(drivesResponse.packets);
    allPassed &= expect(!driveRoot.isEmpty(), QStringLiteral("server should report at least one drive"));

    if (!driveRoot.isEmpty()) {
        const ResponseBundle directoryResponse = sendCommand(host, port, remoteqt::Command::ListDirectory, remoteqt::encodeLocal8Bit(driveRoot));
        allPassed &= expect(directoryResponse.error.isEmpty(), QStringLiteral("list directory request should succeed"));
        allPassed &= expect(validateDirectoryResponse(directoryResponse.packets), QStringLiteral("directory listing should include a terminating packet"));
    }

    const QString invalidDirectory = QStringLiteral("Z:\\__remoteqt_missing_dir__");
    const ResponseBundle invalidDirectoryResponse = sendCommand(host, port, remoteqt::Command::ListDirectory, remoteqt::encodeLocal8Bit(invalidDirectory));
    allPassed &= expect(invalidDirectoryResponse.error.isEmpty(), QStringLiteral("invalid directory request should still respond"));
    allPassed &= expect(invalidDirectoryResponse.packets.size() == 1, QStringLiteral("invalid directory should return one marker packet"));
    if (!invalidDirectoryResponse.packets.isEmpty()) {
        const remoteqt::FileEntry invalidEntry = remoteqt::FileEntry::fromPayload(invalidDirectoryResponse.packets.first().payload);
        allPassed &= expect(invalidEntry.isInvalid, QStringLiteral("invalid directory should be marked invalid"));
        allPassed &= expect(!invalidEntry.hasNext, QStringLiteral("invalid directory marker should terminate the listing"));
    }

    QTemporaryDir tempDir;
    allPassed &= expect(tempDir.isValid(), QStringLiteral("temporary directory should be created"));

    const QString uniqueSuffix = QString::number(QRandomGenerator::global()->generate(), 16);
    const QString downloadSourcePath = tempDir.filePath(QStringLiteral("download-source-%1.txt").arg(uniqueSuffix));
    const QByteArray expectedContent("qt smoke download payload\nline 2");
    {
        QFile source(downloadSourcePath);
        if (!source.open(QIODevice::WriteOnly)) {
            std::cerr << "[FAIL] could not create temp source file" << std::endl;
            return 1;
        }
        source.write(expectedContent);
    }

    allPassed &= validateDownload(host, port, downloadSourcePath, expectedContent);

    const QString unicodeDirName = QStringLiteral("unicode_dir_%1_").arg(uniqueSuffix)
        + QString::fromUcs4(U"\u6D4B\u8BD5");
    const QString unicodeFileName = QStringLiteral("unicode_file_%1_").arg(uniqueSuffix)
        + QString::fromUcs4(U"\u6587\u4EF6")
        + QStringLiteral(".txt");
    const QString unicodeDirPath = tempDir.filePath(unicodeDirName);
    const QString unicodeFilePath = tempDir.filePath(unicodeFileName);
    QDir().mkpath(unicodeDirPath);
    {
        QFile unicodeFile(unicodeFilePath);
        unicodeFile.open(QIODevice::WriteOnly);
        unicodeFile.write("unicode content");
    }
    const ResponseBundle tempDirResponse = sendCommand(host, port, remoteqt::Command::ListDirectory, remoteqt::encodeLocal8Bit(tempDir.path()));
    allPassed &= expect(tempDirResponse.error.isEmpty(), QStringLiteral("listing temp directory with unicode entries should succeed"));
    allPassed &= expect(validateDirectoryResponse(tempDirResponse.packets), QStringLiteral("temp directory listing should include a terminating packet"));
    allPassed &= expect(directoryContainsNames(tempDirResponse.packets, { unicodeDirName, unicodeFileName, QFileInfo(downloadSourcePath).fileName() }),
        QStringLiteral("temp directory listing should preserve unicode names"));
    allPassed &= validateDownload(host, port, unicodeFilePath, QByteArray("unicode content"));

    const QString emptyFilePath = tempDir.filePath(QStringLiteral("empty_%1.txt").arg(uniqueSuffix));
    {
        QFile emptyFile(emptyFilePath);
        emptyFile.open(QIODevice::WriteOnly);
    }
    allPassed &= validateDownload(host, port, emptyFilePath, {});
    allPassed &= validateDownloadFailure(host, port, tempDir.filePath(QStringLiteral("missing_%1.txt").arg(uniqueSuffix)));

    const QString deleteTargetPath = tempDir.filePath(QStringLiteral("delete-target-%1.txt").arg(uniqueSuffix));
    {
        QFile file(deleteTargetPath);
        file.open(QIODevice::WriteOnly);
        file.write("delete me");
    }
    const ResponseBundle deleteResponse = sendCommand(host, port, remoteqt::Command::DeleteFile, remoteqt::encodeLocal8Bit(deleteTargetPath));
    allPassed &= validateStatusReply(deleteResponse, remoteqt::Command::DeleteFile, true, QStringLiteral("delete file request"));
    allPassed &= expect(QFileInfo::exists(deleteTargetPath) == false, QStringLiteral("delete file should remove the target"));

    const QString deleteDirPath = tempDir.filePath(QStringLiteral("delete-dir-%1").arg(uniqueSuffix));
    QDir().mkpath(deleteDirPath + QStringLiteral("/child"));
    {
        QFile nested(deleteDirPath + QStringLiteral("/child/nested.txt"));
        nested.open(QIODevice::WriteOnly);
        nested.write("nested");
    }
    const ResponseBundle deleteDirResponse = sendCommand(host, port, remoteqt::Command::DeleteFile, remoteqt::encodeLocal8Bit(deleteDirPath));
    allPassed &= validateStatusReply(deleteDirResponse, remoteqt::Command::DeleteFile, true, QStringLiteral("delete directory request"));
    allPassed &= expect(QFileInfo::exists(deleteDirPath) == false, QStringLiteral("delete directory should remove recursively"));

    const ResponseBundle deleteMissingResponse = sendCommand(host, port, remoteqt::Command::DeleteFile, remoteqt::encodeLocal8Bit(deleteDirPath));
    allPassed &= validateStatusReply(deleteMissingResponse, remoteqt::Command::DeleteFile, false, QStringLiteral("delete missing target request"));

    const ResponseBundle watchResponse = sendCommand(host, port, remoteqt::Command::WatchScreen);
    allPassed &= expect(watchResponse.error.isEmpty(), QStringLiteral("watch screen request should succeed"));
    allPassed &= expect(watchResponse.packets.size() == 1, QStringLiteral("watch screen should return one image packet"));
    if (!watchResponse.packets.isEmpty()) {
        QImage image;
        const bool imageLoaded = image.loadFromData(watchResponse.packets.first().payload, "PNG");
        allPassed &= expect(imageLoaded, QStringLiteral("watch screen packet should decode as PNG"));
        allPassed &= expect(imageLoaded && !image.isNull(), QStringLiteral("watch screen image should be non-empty"));
    }

    POINT cursorPos {};
    GetCursorPos(&cursorPos);
    remoteqt::MouseEventPacket mouseEvent;
    mouseEvent.action = static_cast<quint16>(remoteqt::MouseAction::Click);
    mouseEvent.button = static_cast<quint16>(remoteqt::MouseButton::None);
    mouseEvent.x = cursorPos.x;
    mouseEvent.y = cursorPos.y;
    const QByteArray mousePayload(reinterpret_cast<const char*>(&mouseEvent), static_cast<int>(sizeof(mouseEvent)));
    const ResponseBundle mouseResponse = sendCommand(host, port, remoteqt::Command::MouseEvent, mousePayload);
    allPassed &= validateStatusReply(mouseResponse, remoteqt::Command::MouseEvent, true, QStringLiteral("mouse event request"));

    const QString executablePath = QStringLiteral("C:/Windows/System32/whoami.exe");
    const ResponseBundle runResponse = sendCommand(host, port, remoteqt::Command::RunFile, remoteqt::encodeLocal8Bit(executablePath));
    allPassed &= validateStatusReply(runResponse, remoteqt::Command::RunFile, true, QStringLiteral("run file request"));

    const ResponseBundle runMissingResponse = sendCommand(host, port, remoteqt::Command::RunFile, remoteqt::encodeLocal8Bit(QStringLiteral("C:/__remoteqt_missing__.exe")));
    allPassed &= validateStatusReply(runMissingResponse, remoteqt::Command::RunFile, false, QStringLiteral("run missing file request"));

    std::cout << (allPassed ? "SMOKE TEST PASSED" : "SMOKE TEST FAILED") << std::endl;
    return allPassed ? 0 : 1;
}
