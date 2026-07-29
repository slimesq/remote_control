#include "common/Packet.h"
#include "common/Protocol.h"

#include <QCursor>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <cstdlib>
#include <future>
#include <iostream>
#include <vector>

namespace
{

constexpr int NetworkTimeoutMs{5000};
constexpr int LargeDownloadSize{256 * 1024 + 37};
constexpr int ConcurrentFileRequestCount{6};
constexpr int ControlCommandCount{2};
constexpr int ExpectedControlResponseCount{ControlCommandCount + 1};

struct ResponseBundle
{
    QList<remote_control::Packet> packets;
    QString error;
};

/**
 * @brief Sends one command and collects all response packets.
 * @param _host Remote server host name or address.
 * @param _port Remote server TCP port.
 * @param _command Protocol command to send.
 * @param _payload Command-specific payload.
 * @return Collected packets and any network error.
 */
ResponseBundle sendCommand(QString const& _host,
                           quint16 _port,
                           remote_control::Command _command,
                           QByteArray const& _payload = {})
{
    ResponseBundle result;
    QTcpSocket socket;
    socket.connectToHost(_host, _port);
    if (!socket.waitForConnected(NetworkTimeoutMs))
    {
        result.error = socket.errorString();
        return result;
    }

    socket.write(remote_control::Packet{_command, _payload}.serialize());
    if (!socket.waitForBytesWritten(NetworkTimeoutMs))
    {
        result.error = socket.errorString();
        return result;
    }

    QByteArray buffer;
    while (socket.state() != QAbstractSocket::UnconnectedState)
    {
        if (!socket.waitForReadyRead(NetworkTimeoutMs))
        {
            if (socket.state() == QAbstractSocket::UnconnectedState)
            {
                break;
            }
            result.error = socket.errorString().isEmpty()
                ? QStringLiteral("Timed out waiting for response")
                : socket.errorString();
            return result;
        }
        buffer.append(socket.readAll());
        while (true)
        {
            auto const packet{remote_control::Packet::tryParse(buffer)};
            if (!packet.has_value())
            {
                break;
            }
            result.packets.push_back(packet.value());
        }
    }

    if (!buffer.isEmpty())
    {
        while (true)
        {
            auto const packet{remote_control::Packet::tryParse(buffer)};
            if (!packet.has_value())
            {
                break;
            }
            result.packets.push_back(packet.value());
        }
    }

    return result;
}

/**
 * @brief Requests multiple screen frames over one persistent TCP connection.
 * @param _host Remote server host name or address.
 * @param _port Remote server TCP port.
 * @param _frameCount Number of frames to request sequentially.
 * @return Collected frame packets and any network or protocol error.
 */
ResponseBundle requestWatchFrames(QString const& _host, quint16 _port, int _frameCount)
{
    ResponseBundle result;
    QTcpSocket socket;
    socket.connectToHost(_host, _port);
    if (!socket.waitForConnected(NetworkTimeoutMs))
    {
        result.error = socket.errorString();
        return result;
    }

    QByteArray buffer;
    for (int frameIndex{0}; frameIndex < _frameCount; ++frameIndex)
    {
        QByteArray const request{
            remote_control::Packet{remote_control::Command::WatchScreen}.serialize()};
        if (socket.write(request) < 0 || !socket.waitForBytesWritten(NetworkTimeoutMs))
        {
            result.error = socket.errorString();
            return result;
        }

        // Keep exactly one frame outstanding so the test matches the client flow-control rule.
        while (result.packets.size() <= frameIndex)
        {
            auto const packet{remote_control::Packet::tryParse(buffer)};
            if (packet.has_value())
            {
                if (packet->command != remote_control::Command::WatchScreen)
                {
                    result.error = QStringLiteral("Unexpected monitor response command");
                    return result;
                }
                result.packets.push_back(packet.value());
                continue;
            }

            if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(NetworkTimeoutMs))
            {
                result.error = socket.errorString().isEmpty()
                    ? QStringLiteral("Timed out waiting for monitor frame")
                    : socket.errorString();
                return result;
            }
            buffer.append(socket.readAll());
        }
    }

    socket.disconnectFromHost();
    if (socket.state() != QAbstractSocket::UnconnectedState)
    {
        socket.waitForDisconnected(NetworkTimeoutMs);
    }
    return result;
}

/**
 * @brief Exchanges one request and response packet on an existing connection.
 * @param _socket Connected socket used for the exchange.
 * @param _buffer Persistent receive buffer for partial packets.
 * @param _request Request packet to send.
 * @param _responseOut Output for the parsed response packet.
 * @param _errorOut Output for a network or protocol error.
 * @return true when one complete response is received; otherwise false.
 */
bool exchangePacket(QTcpSocket* _socket,
                    QByteArray* _buffer,
                    remote_control::Packet const& _request,
                    remote_control::Packet* _responseOut,
                    QString* _errorOut)
{
    QByteArray const bytes{_request.serialize()};
    if (_socket->write(bytes) < 0 || !_socket->waitForBytesWritten(NetworkTimeoutMs))
    {
        *_errorOut = _socket->errorString();
        return false;
    }

    while (true)
    {
        auto const response{remote_control::Packet::tryParse(*_buffer)};
        if (response.has_value())
        {
            *_responseOut = response.value();
            return true;
        }
        if (_socket->bytesAvailable() == 0 && !_socket->waitForReadyRead(NetworkTimeoutMs))
        {
            *_errorOut = _socket->errorString().isEmpty()
                ? QStringLiteral("Timed out waiting for a control response")
                : _socket->errorString();
            return false;
        }
        _buffer->append(_socket->readAll());
    }
}

/**
 * @brief Sends ordered commands through one persistent control connection.
 * @param _host Remote server host name or address.
 * @param _port Remote server TCP port.
 * @param _commands Commands to send after the control-channel handshake.
 * @return Handshake and command responses, plus any network error.
 */
ResponseBundle requestControlCommands(QString const& _host,
                                      quint16 _port,
                                      QList<remote_control::Packet> const& _commands)
{
    ResponseBundle result;
    QTcpSocket socket;
    socket.connectToHost(_host, _port);
    if (!socket.waitForConnected(NetworkTimeoutMs))
    {
        result.error = socket.errorString();
        return result;
    }

    QByteArray buffer;
    remote_control::Packet response;
    if (!exchangePacket(&socket,
                        &buffer,
                        remote_control::Packet{remote_control::Command::ControlChannel},
                        &response,
                        &result.error))
    {
        return result;
    }
    result.packets.push_back(response);

    for (remote_control::Packet const& command : _commands)
    {
        if (!exchangePacket(&socket, &buffer, command, &response, &result.error))
        {
            return result;
        }
        result.packets.push_back(response);
    }

    socket.disconnectFromHost();
    if (socket.state() != QAbstractSocket::UnconnectedState)
    {
        socket.waitForDisconnected(NetworkTimeoutMs);
    }
    return result;
}

/**
 * @brief Records whether a smoke-test condition passed.
 * @param _condition Condition to verify.
 * @param _message Test result message.
 * @return The supplied condition.
 */
bool expect(bool _condition, QString const& _message)
{
    if (!_condition)
    {
        std::cerr << "[FAIL] " << _message.toStdString() << std::endl;
        return false;
    }
    std::cout << "[ OK ] " << _message.toStdString() << std::endl;
    return true;
}

/**
 * @brief Selects the first drive from a drive-list response.
 * @param _packets Drive-list response packets.
 * @return First normalized drive root, or an empty string.
 */
QString pickDrive(QList<remote_control::Packet> const& _packets)
{
    if (_packets.isEmpty())
    {
        return {};
    }
    QStringList const drives{
        remote_control::decodeUtf8(_packets.first().payload).split(',', Qt::SkipEmptyParts)};
    if (drives.isEmpty())
    {
        return {};
    }
    QString drive{drives.first().trimmed()};
    if (!drive.endsWith(':'))
    {
        drive.append(':');
    }
    return drive + '\\';
}

/**
 * @brief Validates a complete streamed directory response.
 * @param _packets Directory response packets.
 * @return true when the response is valid and terminated; otherwise false.
 */
bool validateDirectoryResponse(QList<remote_control::Packet> const& _packets)
{
    if (_packets.isEmpty())
    {
        return false;
    }
    bool sawEnd{false};
    for (remote_control::Packet const& packet : _packets)
    {
        if (packet.command != remote_control::Command::ListDirectory)
        {
            return false;
        }
        remote_control::FileEntry const entry{
            remote_control::FileEntry::fromPayload(packet.payload)};
        if (!entry.hasNext)
        {
            sawEnd = true;
        }
    }
    return sawEnd;
}

/**
 * @brief Verifies packets returned by a successful file download.
 * @param _response Download response to validate.
 * @param _expectedContent Expected file contents.
 * @param _label Label used in test output.
 * @return true when the downloaded content matches; otherwise false.
 */
bool validateDownloadResponse(ResponseBundle const& _response,
                              QByteArray const& _expectedContent,
                              QString const& _label)
{
    if (!expect(_response.error.isEmpty(), _label + QStringLiteral(": request should succeed")))
    {
        return false;
    }
    if (!expect(!_response.packets.isEmpty(), _label + QStringLiteral(": should return packets")))
    {
        return false;
    }
    if (!expect(_response.packets.first().payload.size() == static_cast<int>(sizeof(qint64)),
                _label + QStringLiteral(": should start with length header")))
    {
        return false;
    }

    qint64 expectedSize{-1};
    {
        QDataStream stream{_response.packets.first().payload};
        stream.setByteOrder(QDataStream::LittleEndian);
        stream >> expectedSize;
    }
    if (!expect(expectedSize == _expectedContent.size(),
                _label + QStringLiteral(": length should match source file")))
    {
        return false;
    }

    QByteArray downloaded;
    for (int index{1}; index < _response.packets.size(); ++index)
    {
        downloaded.append(_response.packets[index].payload);
    }
    return expect(downloaded == _expectedContent,
                  _label + QStringLiteral(": content should match"));
}

/**
 * @brief Requests and verifies one successful file download.
 * @param _host Remote server host name or address.
 * @param _port Remote server TCP port.
 * @param _remotePath Remote file path to download.
 * @param _expectedContent Expected file contents.
 * @return true when the downloaded content matches; otherwise false.
 */
bool validateDownload(QString const& _host,
                      quint16 _port,
                      QString const& _remotePath,
                      QByteArray const& _expectedContent)
{
    ResponseBundle const response{sendCommand(_host,
                                              _port,
                                              remote_control::Command::DownloadFile,
                                              remote_control::encodeUtf8(_remotePath))};
    return validateDownloadResponse(response, _expectedContent, QStringLiteral("download request"));
}

/**
 * @brief Verifies a failed file download response.
 * @param _host Remote server host name or address.
 * @param _port Remote server TCP port.
 * @param _remotePath Missing or unreadable remote file path.
 * @return true when the server reports failure correctly; otherwise false.
 */
bool validateDownloadFailure(QString const& _host, quint16 _port, QString const& _remotePath)
{
    ResponseBundle const response{sendCommand(_host,
                                              _port,
                                              remote_control::Command::DownloadFile,
                                              remote_control::encodeUtf8(_remotePath))};
    if (!expect(response.error.isEmpty(),
                QStringLiteral("download failure request should still return a response")))
    {
        return false;
    }
    if (!expect(response.packets.size() == 1,
                QStringLiteral("failed download should return one header packet")))
    {
        return false;
    }

    qint64 encodedSize{0};
    QDataStream stream{response.packets.first().payload};
    stream.setByteOrder(QDataStream::LittleEndian);
    stream >> encodedSize;
    return expect(encodedSize < 0,
                  QStringLiteral("failed download should return a negative length marker"));
}

/**
 * @brief Checks whether a directory response contains the expected names.
 * @param _packets Directory response packets.
 * @param _names Entry names expected in the response.
 * @return true when every expected name is present; otherwise false.
 */
bool directoryContainsNames(QList<remote_control::Packet> const& _packets,
                            QStringList const& _names)
{
    QStringList actualNames;
    for (remote_control::Packet const& packet : _packets)
    {
        remote_control::FileEntry const entry{
            remote_control::FileEntry::fromPayload(packet.payload)};
        if (entry.hasNext && !entry.isInvalid)
        {
            actualNames << entry.fileName;
        }
    }

    for (QString const& name : _names)
    {
        if (!actualNames.contains(name))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Validates a common command-status response.
 * @param _response Response packets and network error.
 * @param _command Expected response command.
 * @param _expectedSuccess Expected status value.
 * @param _label Label used in test output.
 * @return true when the response matches all expectations; otherwise false.
 */
bool validateStatusReply(ResponseBundle const& _response,
                         remote_control::Command _command,
                         bool _expectedSuccess,
                         QString const& _label)
{
    if (!expect(_response.error.isEmpty(),
                _label + QStringLiteral(": network request should succeed")))
    {
        return false;
    }
    if (!expect(_response.packets.size() == 1,
                _label + QStringLiteral(": should return one packet")))
    {
        return false;
    }
    if (!expect(!_response.packets.isEmpty() && _response.packets.first().command == _command,
                _label + QStringLiteral(": should echo the command")))
    {
        return false;
    }
    QString message;
    bool const success{
        remote_control::parseStatusPayload(_response.packets.first().payload, &message)};
    return expect(success == _expectedSuccess,
                  _label + QStringLiteral(": status payload should match expected result"));
}

}  // namespace

/**
 * @brief Runs end-to-end smoke tests against a running server.
 * @param argc Number of command-line arguments.
 * @param argv Command-line argument array.
 * @return EXIT_SUCCESS when all tests pass; otherwise EXIT_FAILURE.
 */
int main(int argc, char* argv[])
{
    QGuiApplication const app{argc, argv};

    QString const host{argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("127.0.0.1")};
    quint16 const port{argc > 2 ? QString::fromLocal8Bit(argv[2]).toUShort()
                                : static_cast<quint16>(9527)};

    bool allPassed{true};

    ResponseBundle const testResponse{
        sendCommand(host, port, remote_control::Command::TestConnection)};
    allPassed &= expect(testResponse.error.isEmpty(),
                        QStringLiteral("test connection request should succeed"));
    allPassed &= expect(testResponse.packets.size() == 1,
                        QStringLiteral("test connection should return one packet"));
    allPassed &=
        expect(!testResponse.packets.isEmpty() &&
                   testResponse.packets.first().command == remote_control::Command::TestConnection,
               QStringLiteral("test connection should echo command 1981"));

    ResponseBundle const drivesResponse{
        sendCommand(host, port, remote_control::Command::ListDrives)};
    allPassed &= expect(drivesResponse.error.isEmpty(),
                        QStringLiteral("list drives request should succeed"));
    QString const driveRoot{pickDrive(drivesResponse.packets)};
    allPassed &=
        expect(!driveRoot.isEmpty(), QStringLiteral("server should report at least one drive"));

    if (!driveRoot.isEmpty())
    {
        ResponseBundle const directoryResponse{sendCommand(host,
                                                           port,
                                                           remote_control::Command::ListDirectory,
                                                           remote_control::encodeUtf8(driveRoot))};
        allPassed &= expect(directoryResponse.error.isEmpty(),
                            QStringLiteral("list directory request should succeed"));
        allPassed &=
            expect(validateDirectoryResponse(directoryResponse.packets),
                   QStringLiteral("directory listing should include a terminating packet"));
    }

    QString const invalidDirectory{QStringLiteral("Z:\\__remote_control_missing_dir__")};
    ResponseBundle const invalidDirectoryResponse{
        sendCommand(host,
                    port,
                    remote_control::Command::ListDirectory,
                    remote_control::encodeUtf8(invalidDirectory))};
    allPassed &= expect(invalidDirectoryResponse.error.isEmpty(),
                        QStringLiteral("invalid directory request should still respond"));
    allPassed &= expect(invalidDirectoryResponse.packets.size() == 1,
                        QStringLiteral("invalid directory should return one marker packet"));
    if (!invalidDirectoryResponse.packets.isEmpty())
    {
        remote_control::FileEntry const invalidEntry{remote_control::FileEntry::fromPayload(
            invalidDirectoryResponse.packets.first().payload)};
        allPassed &= expect(invalidEntry.isInvalid,
                            QStringLiteral("invalid directory should be marked invalid"));
        allPassed &=
            expect(!invalidEntry.hasNext,
                   QStringLiteral("invalid directory marker should terminate the listing"));
    }

    QTemporaryDir const tempDir;
    allPassed &= expect(tempDir.isValid(), QStringLiteral("temporary directory should be created"));

    QString const uniqueSuffix{QString::number(QRandomGenerator::global()->generate(), 16)};
    QString const downloadSourcePath{
        tempDir.filePath(QStringLiteral("download-source-%1.txt").arg(uniqueSuffix))};
    QByteArray const downloadPattern{QByteArrayLiteral("0123456789ABCDEF")};
    QByteArray const expectedContent{
        downloadPattern.repeated(LargeDownloadSize / downloadPattern.size() + 1)
            .left(LargeDownloadSize)};
    {
        QFile source{downloadSourcePath};
        if (!source.open(QIODevice::WriteOnly))
        {
            std::cerr << "[FAIL] could not create temp source file" << std::endl;
            return EXIT_FAILURE;
        }
        source.write(expectedContent);
    }

    allPassed &= validateDownload(host, port, downloadSourcePath, expectedContent);

    // Submit more requests than the server's maximum file-worker count to exercise its queue.
    std::vector<std::future<ResponseBundle>> concurrentDownloads;
    concurrentDownloads.reserve(ConcurrentFileRequestCount);
    for (int requestIndex{0}; requestIndex < ConcurrentFileRequestCount; ++requestIndex)
    {
        concurrentDownloads.emplace_back(std::async(std::launch::async, [=] {
            return sendCommand(host,
                               port,
                               remote_control::Command::DownloadFile,
                               remote_control::encodeUtf8(downloadSourcePath));
        }));
    }
    for (int requestIndex{0}; requestIndex < ConcurrentFileRequestCount; ++requestIndex)
    {
        allPassed &= validateDownloadResponse(
            concurrentDownloads[requestIndex].get(),
            expectedContent,
            QStringLiteral("concurrent download %1").arg(requestIndex + 1));
    }

    QString const unicodeDirName{QStringLiteral("unicode_dir_%1_").arg(uniqueSuffix) +
                                 QString::fromUcs4(U"\u6D4B\u8BD5")};
    QString const unicodeFileName{QStringLiteral("unicode_file_%1_").arg(uniqueSuffix) +
                                  QString::fromUcs4(U"\u6587\u4EF6") + QStringLiteral(".txt")};
    QString const unicodeDirPath{tempDir.filePath(unicodeDirName)};
    QString const unicodeFilePath{tempDir.filePath(unicodeFileName)};
    QDir{}.mkpath(unicodeDirPath);
    {
        QFile unicodeFile{unicodeFilePath};
        unicodeFile.open(QIODevice::WriteOnly);
        unicodeFile.write("unicode content");
    }
    ResponseBundle const tempDirResponse{sendCommand(host,
                                                     port,
                                                     remote_control::Command::ListDirectory,
                                                     remote_control::encodeUtf8(tempDir.path()))};
    allPassed &=
        expect(tempDirResponse.error.isEmpty(),
               QStringLiteral("listing temp directory with unicode entries should succeed"));
    allPassed &=
        expect(validateDirectoryResponse(tempDirResponse.packets),
               QStringLiteral("temp directory listing should include a terminating packet"));
    allPassed &=
        expect(directoryContainsNames(
                   tempDirResponse.packets,
                   {unicodeDirName, unicodeFileName, QFileInfo{downloadSourcePath}.fileName()}),
               QStringLiteral("temp directory listing should preserve unicode names"));
    allPassed &= validateDownload(host, port, unicodeFilePath, QByteArray{"unicode content"});

    QString const emptyFilePath{tempDir.filePath(QStringLiteral("empty_%1.txt").arg(uniqueSuffix))};
    {
        QFile emptyFile{emptyFilePath};
        emptyFile.open(QIODevice::WriteOnly);
    }
    allPassed &= validateDownload(host, port, emptyFilePath, {});
    allPassed &= validateDownloadFailure(
        host, port, tempDir.filePath(QStringLiteral("missing_%1.txt").arg(uniqueSuffix)));

    QString const deleteTargetPath{
        tempDir.filePath(QStringLiteral("delete-target-%1.txt").arg(uniqueSuffix))};
    {
        QFile file{deleteTargetPath};
        file.open(QIODevice::WriteOnly);
        file.write("delete me");
    }
    ResponseBundle const deleteResponse{sendCommand(host,
                                                    port,
                                                    remote_control::Command::DeleteFile,
                                                    remote_control::encodeUtf8(deleteTargetPath))};
    allPassed &= validateStatusReply(deleteResponse,
                                     remote_control::Command::DeleteFile,
                                     true,
                                     QStringLiteral("delete file request"));
    allPassed &= expect(!QFileInfo::exists(deleteTargetPath),
                        QStringLiteral("delete file should remove the target"));

    QString const deleteDirPath{
        tempDir.filePath(QStringLiteral("delete-dir-%1").arg(uniqueSuffix))};
    QDir{}.mkpath(deleteDirPath + QStringLiteral("/child"));
    {
        QFile nested{deleteDirPath + QStringLiteral("/child/nested.txt")};
        nested.open(QIODevice::WriteOnly);
        nested.write("nested");
    }
    ResponseBundle const deleteDirResponse{sendCommand(host,
                                                       port,
                                                       remote_control::Command::DeleteFile,
                                                       remote_control::encodeUtf8(deleteDirPath))};
    allPassed &= validateStatusReply(deleteDirResponse,
                                     remote_control::Command::DeleteFile,
                                     true,
                                     QStringLiteral("delete directory request"));
    allPassed &= expect(!QFileInfo::exists(deleteDirPath),
                        QStringLiteral("delete directory should remove recursively"));

    ResponseBundle const deleteMissingResponse{
        sendCommand(host,
                    port,
                    remote_control::Command::DeleteFile,
                    remote_control::encodeUtf8(deleteDirPath))};
    allPassed &= validateStatusReply(deleteMissingResponse,
                                     remote_control::Command::DeleteFile,
                                     false,
                                     QStringLiteral("delete missing target request"));

    constexpr int WatchFrameCount{2};
    ResponseBundle const watchResponse{requestWatchFrames(host, port, WatchFrameCount)};
    allPassed &= expect(watchResponse.error.isEmpty(),
                        QStringLiteral("persistent watch screen requests should succeed"));
    allPassed &= expect(watchResponse.packets.size() == WatchFrameCount,
                        QStringLiteral("one monitor connection should return two image packets"));
    for (remote_control::Packet const& packet : watchResponse.packets)
    {
        QImage image;
        bool const imageLoaded{image.loadFromData(packet.payload, "PNG")};
        allPassed &=
            expect(imageLoaded, QStringLiteral("watch screen packet should decode as PNG"));
        allPassed &= expect(imageLoaded && !image.isNull(),
                            QStringLiteral("watch screen image should be non-empty"));
    }

    QPoint const cursorPosition{QCursor::pos()};
    remote_control::MouseEventPacket mouseEvent;
    mouseEvent.action = static_cast<quint16>(remote_control::MouseAction::Click);
    mouseEvent.button = static_cast<quint16>(remote_control::MouseButton::None);
    mouseEvent.x = cursorPosition.x();
    mouseEvent.y = cursorPosition.y();
    QByteArray const mousePayload{reinterpret_cast<char const*>(&mouseEvent),
                                  static_cast<int>(sizeof(mouseEvent))};
    ResponseBundle const controlResponse{requestControlCommands(
        host,
        port,
        {remote_control::Packet{remote_control::Command::MouseEvent, mousePayload},
         remote_control::Packet{remote_control::Command::MouseEvent, mousePayload}})};
    allPassed &= expect(controlResponse.error.isEmpty(),
                        QStringLiteral("persistent control requests should succeed"));
    allPassed &= expect(controlResponse.packets.size() == ExpectedControlResponseCount,
                        QStringLiteral("one control connection should return three responses"));
    if (controlResponse.packets.size() == ExpectedControlResponseCount)
    {
        allPassed &= expect(
            controlResponse.packets.first().command == remote_control::Command::ControlChannel,
            QStringLiteral("control channel should acknowledge its handshake"));
        for (int responseIndex{1}; responseIndex < controlResponse.packets.size(); ++responseIndex)
        {
            QString message;
            remote_control::Packet const& response{controlResponse.packets[responseIndex]};
            bool const success{remote_control::parseStatusPayload(response.payload, &message)};
            allPassed &= expect(response.command == remote_control::Command::MouseEvent && success,
                                QStringLiteral("control channel should execute mouse events"));
        }
    }

    QString const executablePath{QStringLiteral("C:/Windows/System32/whoami.exe")};
    ResponseBundle const runResponse{sendCommand(
        host, port, remote_control::Command::RunFile, remote_control::encodeUtf8(executablePath))};
    allPassed &= validateStatusReply(
        runResponse, remote_control::Command::RunFile, true, QStringLiteral("run file request"));

    ResponseBundle const runMissingResponse{sendCommand(
        host,
        port,
        remote_control::Command::RunFile,
        remote_control::encodeUtf8(QStringLiteral("C:/__remote_control_missing__.exe")))};
    allPassed &= validateStatusReply(runMissingResponse,
                                     remote_control::Command::RunFile,
                                     false,
                                     QStringLiteral("run missing file request"));

    std::cout << (allPassed ? "SMOKE TEST PASSED" : "SMOKE TEST FAILED") << std::endl;
    return allPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
