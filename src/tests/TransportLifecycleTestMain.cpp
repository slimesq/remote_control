#include "server/RemoteControlTransport.h"

#include <QCoreApplication>
#include <QTcpSocket>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace
{

constexpr int LifecycleIterationCount{40};
constexpr int ConnectionAttemptCount{24};
constexpr int ConnectionTimeoutMs{100};

/**
 * @brief Connects clients while the server begins its shutdown sequence.
 * @param _port Ephemeral server port used by this iteration.
 * @param _started Set after the connector thread is ready to race with shutdown.
 */
void connectDuringShutdown(quint16 _port, std::atomic_bool* _started)
{
    std::vector<std::unique_ptr<QTcpSocket>> sockets;
    sockets.reserve(ConnectionAttemptCount);
    _started->store(true);
    for (int index{0}; index < ConnectionAttemptCount; ++index)
    {
        auto socket{std::make_unique<QTcpSocket>()};
        socket->connectToHost(QStringLiteral("127.0.0.1"), _port);
        if (!socket->waitForConnected(ConnectionTimeoutMs))
        {
            break;
        }

        // Leave an incomplete packet in flight so shutdown also cancels pending receives.
        socket->write(QByteArray{"\xFF", 1});
        static_cast<void>(socket->waitForBytesWritten(ConnectionTimeoutMs));
        sockets.push_back(std::move(socket));
    }
}

/**
 * @brief Exercises listener and accepted-socket shutdown while connections are arriving.
 * @return true when every server instance starts and stops cleanly; otherwise false.
 */
bool testConcurrentStop()
{
    for (int iteration{0}; iteration < LifecycleIterationCount; ++iteration)
    {
        RemoteControlTransport server{nullptr};
        if (!server.start(0) || server.listeningPort() == 0)
        {
            std::cerr << "FAILED: lifecycle server did not start at iteration " << iteration
                      << std::endl;
            return false;
        }

        std::atomic_bool connectorStarted{false};
        std::thread connector{connectDuringShutdown, server.listeningPort(), &connectorStarted};
        while (!connectorStarted.load())
        {
            std::this_thread::yield();
        }

        server.stop();
        connector.join();
        if (server.listeningPort() != 0)
        {
            std::cerr << "FAILED: lifecycle server retained its port at iteration " << iteration
                      << std::endl;
            return false;
        }
    }
    return true;
}

}  // namespace

/**
 * @brief Runs IOCP server lifecycle stress tests.
 * @param argc Process argument count.
 * @param argv Process argument values.
 * @return EXIT_SUCCESS when every lifecycle iteration passes; otherwise EXIT_FAILURE.
 */
int main(int argc, char* argv[])
{
    QCoreApplication const application{argc, argv};
    bool const passed{testConcurrentStop()};
    std::cout << (passed ? "SERVER LIFECYCLE TESTS PASSED" : "SERVER LIFECYCLE TESTS FAILED")
              << std::endl;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
