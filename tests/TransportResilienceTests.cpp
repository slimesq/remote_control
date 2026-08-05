#include "common/Packet.h"
#include "FakeRemoteControlHostServices.h"
#include "RemoteControlTransport.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTcpSocket>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace
{

constexpr int NetworkTimeoutMs{2000};
constexpr int StressThreadCount{8};
constexpr int RequestsPerStressThread{16};
constexpr qsizetype OversizedPayloadIncrement{5};

/**
 * @brief Waits for one complete protocol packet on a connected socket.
 * @param _socket Socket receiving the response.
 * @param _packetOut Parsed response destination.
 * @return true when a complete packet arrives before the deadline; otherwise false.
 */
bool waitForPacket(QTcpSocket* _socket, remote_control::Packet* _packetOut)
{
    QByteArray buffer;
    QElapsedTimer deadline;
    deadline.start();
    while (deadline.elapsed() < NetworkTimeoutMs)
    {
        buffer.append(_socket->readAll());
        auto packet{remote_control::Packet::tryParse(buffer)};
        if (packet.has_value())
        {
            *_packetOut = std::move(packet.value());
            return true;
        }
        int const remaining{NetworkTimeoutMs - static_cast<int>(deadline.elapsed())};
        if (remaining <= 0 || !_socket->waitForReadyRead(remaining))
        {
            buffer.append(_socket->readAll());
            packet = remote_control::Packet::tryParse(buffer);
            if (packet.has_value())
            {
                *_packetOut = std::move(packet.value());
                return true;
            }
            return false;
        }
    }
    return false;
}

/**
 * @brief Sends bytes and verifies a valid connection-test response.
 * @param _port Ephemeral server port.
 * @param _bytes Bytes containing malformed prefixes and a final valid request.
 * @return true when the server resynchronizes and responds; otherwise false.
 */
bool expectConnectionTestResponse(quint16 _port, QByteArray const& _bytes)
{
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), _port);
    if (!socket.waitForConnected(NetworkTimeoutMs))
    {
        return false;
    }
    if (socket.write(_bytes) != _bytes.size() || !socket.waitForBytesWritten(NetworkTimeoutMs))
    {
        return false;
    }
    remote_control::Packet response;
    return waitForPacket(&socket, &response) &&
        response.command == remote_control::Command::TestConnection && response.payload.isEmpty();
}

/**
 * @brief Builds a frame whose declared payload length exceeds the protocol limit.
 * @return Malformed serialized header used for deterministic fault injection.
 */
QByteArray makeOversizedFrameHeader()
{
    QByteArray bytes;
    QDataStream stream{&bytes, QIODevice::WriteOnly};
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << remote_control::Packet::Header;
    stream << static_cast<quint32>(remote_control::Packet::MaximumPayloadSize +
                                   OversizedPayloadIncrement);
    stream << static_cast<quint16>(remote_control::Command::TestConnection);
    stream << static_cast<quint16>(0);
    return bytes;
}

/**
 * @brief Exercises parser recovery and illegal state transitions with live TCP connections.
 * @param _port Ephemeral server port.
 * @return true when all faults are isolated and the server remains responsive.
 */
bool testFaultInjection(quint16 _port)
{
    QByteArray const validRequest{
        remote_control::Packet{remote_control::Command::TestConnection}.serialize()};

    QByteArray garbagePrefixed{"garbage-before-header"};
    garbagePrefixed.append(validRequest);
    if (!expectConnectionTestResponse(_port, garbagePrefixed))
    {
        std::cerr << "FAILED: parser did not recover from a garbage prefix" << std::endl;
        return false;
    }

    QByteArray badChecksum{validRequest};
    badChecksum[badChecksum.size() - 1] = static_cast<char>(1);
    badChecksum.append(validRequest);
    if (!expectConnectionTestResponse(_port, badChecksum))
    {
        std::cerr << "FAILED: parser did not recover from a bad checksum" << std::endl;
        return false;
    }

    QByteArray oversized{makeOversizedFrameHeader()};
    oversized.append(validRequest);
    if (!expectConnectionTestResponse(_port, oversized))
    {
        std::cerr << "FAILED: parser did not recover from an oversized length" << std::endl;
        return false;
    }

    {
        QTcpSocket partialSocket;
        partialSocket.connectToHost(QStringLiteral("127.0.0.1"), _port);
        if (!partialSocket.waitForConnected(NetworkTimeoutMs))
        {
            return false;
        }
        partialSocket.write(QByteArray{"\xFF\xFE\x04", 3});
        static_cast<void>(partialSocket.waitForBytesWritten(NetworkTimeoutMs));
        partialSocket.abort();
    }
    if (!expectConnectionTestResponse(_port, validRequest))
    {
        std::cerr << "FAILED: partial-frame disconnect affected later connections" << std::endl;
        return false;
    }

    {
        QTcpSocket stateSocket;
        stateSocket.connectToHost(QStringLiteral("127.0.0.1"), _port);
        if (!stateSocket.waitForConnected(NetworkTimeoutMs))
        {
            return false;
        }
        QByteArray const handshakeRequest{
            remote_control::Packet{remote_control::Command::ControlChannel}.serialize()};
        stateSocket.write(handshakeRequest);
        if (!stateSocket.waitForBytesWritten(NetworkTimeoutMs))
        {
            return false;
        }
        remote_control::Packet handshake;
        if (!waitForPacket(&stateSocket, &handshake) ||
            handshake.command != remote_control::Command::ControlChannel)
        {
            std::cerr << "FAILED: control handshake did not complete before phase fault"
                      << std::endl;
            return false;
        }
        stateSocket.write(validRequest);
        if (!stateSocket.waitForBytesWritten(NetworkTimeoutMs))
        {
            return false;
        }
        remote_control::Packet rejectedResponse;
        QString rejectionMessage;
        if (!waitForPacket(&stateSocket, &rejectedResponse) ||
            rejectedResponse.command != remote_control::Command::TestConnection ||
            remote_control::parseStatusPayload(rejectedResponse.payload, &rejectionMessage))
        {
            std::cerr << "FAILED: command invalid for ControlStream was not explicitly rejected"
                      << std::endl;
            return false;
        }
        stateSocket.abort();
    }

    return expectConnectionTestResponse(_port, validRequest);
}

/**
 * @brief Sends many concurrent one-shot requests through independent connections.
 * @param _port Ephemeral server port.
 * @return true when every request receives its matching response; otherwise false.
 */
bool testConcurrentOneShotStress(quint16 _port)
{
    QByteArray const request{
        remote_control::Packet{remote_control::Command::TestConnection}.serialize()};
    std::atomic_int successfulRequests{0};
    std::vector<std::thread> threads;
    threads.reserve(StressThreadCount);
    for (int threadIndex{0}; threadIndex < StressThreadCount; ++threadIndex)
    {
        threads.emplace_back([_port, &request, &successfulRequests] {
            for (int requestIndex{0}; requestIndex < RequestsPerStressThread; ++requestIndex)
            {
                if (expectConnectionTestResponse(_port, request))
                {
                    successfulRequests.fetch_add(1);
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    int constexpr ExpectedRequestCount{StressThreadCount * RequestsPerStressThread};
    if (successfulRequests.load() != ExpectedRequestCount)
    {
        std::cerr << "FAILED: only " << successfulRequests.load() << " of " << ExpectedRequestCount
                  << " concurrent requests succeeded" << std::endl;
        return false;
    }
    return true;
}

}  // namespace

/**
 * @brief Runs live IOCP stress and deterministic network fault-injection tests.
 * @param argc Process argument count.
 * @param argv Process argument values.
 * @return EXIT_SUCCESS when the transport remains responsive; otherwise EXIT_FAILURE.
 */
int main(int argc, char* argv[])
{
    QCoreApplication const application{argc, argv};
    QLoggingCategory::setFilterRules(QStringLiteral("remote_control.server.debug=false"));

    RemoteControlTransport server{fakeRemoteControlHostServices()};
    if (!server.start(0) || server.listeningPort() == 0)
    {
        std::cerr << "FAILED: resilience server did not start" << std::endl;
        return EXIT_FAILURE;
    }

    bool const passed{testFaultInjection(server.listeningPort()) &&
                      testConcurrentOneShotStress(server.listeningPort())};
    server.stop();
    std::cout << (passed ? "TRANSPORT RESILIENCE TESTS PASSED"
                         : "TRANSPORT RESILIENCE TESTS FAILED")
              << std::endl;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
