#include "server/RemoteControlTransportInternal.h"

#include <QCoreApplication>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace
{

using iocp_detail::ConnectionPhase;
using iocp_detail::ConnectionRegistry;
using iocp_detail::ConnectionStateMachine;

constexpr std::size_t RegistryCapacity{4};
constexpr SOCKET FirstTestSocket{1};
constexpr SOCKET SecondTestSocket{2};
constexpr SOCKET ThirdTestSocket{3};
constexpr SOCKET FourthTestSocket{4};
constexpr SOCKET OverflowTestSocket{5};

/**
 * @brief Reports one failed state-machine expectation.
 * @param _condition Condition that must hold.
 * @param _message Failure description.
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
 * @brief Verifies classification is one-way and closing is idempotent under contention.
 * @return true when every transition follows the lifecycle contract; otherwise false.
 */
bool testStateTransitions()
{
    bool passed{true};
    ConnectionStateMachine state;
    passed &= expect(state.phase() == ConnectionPhase::AwaitingRequest,
                     "a new connection must await its first request");
    passed &= expect(!state.tryClassify(ConnectionPhase::AwaitingRequest),
                     "AwaitingRequest is not a classification target");
    passed &= expect(state.tryClassify(ConnectionPhase::ScreenStream),
                     "the first valid classification must succeed");
    passed &= expect(!state.tryClassify(ConnectionPhase::ControlStream),
                     "a classified connection must not change protocol roles");

    constexpr int ClosingContenderCount{16};
    std::atomic_int closingWinners{0};
    std::atomic_int previousScreenPhases{0};
    std::vector<std::thread> contenders;
    contenders.reserve(ClosingContenderCount);
    for (int index{0}; index < ClosingContenderCount; ++index)
    {
        contenders.emplace_back([&state, &closingWinners, &previousScreenPhases] {
            ConnectionPhase previousPhase{ConnectionPhase::Closed};
            if (state.tryBeginClosing(&previousPhase))
            {
                closingWinners.fetch_add(1);
                if (previousPhase == ConnectionPhase::ScreenStream)
                {
                    previousScreenPhases.fetch_add(1);
                }
            }
        });
    }
    for (std::thread& contender : contenders)
    {
        contender.join();
    }

    passed &=
        expect(closingWinners.load() == 1, "exactly one thread must win the Closing transition");
    passed &= expect(previousScreenPhases.load() == 1,
                     "the winning transition must preserve its previous role");
    passed &= expect(state.phase() == ConnectionPhase::Closing,
                     "the winner must leave the state machine in Closing");
    passed &= expect(state.isTerminal(), "Closing must reject new protocol and I/O work");
    state.markClosed();
    passed &= expect(state.phase() == ConnectionPhase::Closed,
                     "markClosed must complete the terminal transition");
    passed &= expect(!state.tryBeginClosing(nullptr), "Closed must remain idempotent");
    return passed;
}

/**
 * @brief Verifies total capacity, role quotas, and quota release after removal.
 * @return true when registry invariants remain consistent; otherwise false.
 */
bool testConnectionRegistry()
{
    bool passed{true};
    ConnectionRegistry registry{RegistryCapacity, 1, 1};
    auto const first{registry.add(FirstTestSocket)};
    auto const second{registry.add(SecondTestSocket)};
    auto const third{registry.add(ThirdTestSocket)};
    auto const fourth{registry.add(FourthTestSocket)};
    passed &=
        expect(first && second && third && fourth, "registry capacity must accept four entries");
    passed &= expect(!registry.add(OverflowTestSocket),
                     "registry must reject entries beyond total capacity");
    passed &= expect(registry.tryClassify(first, ConnectionPhase::ScreenStream),
                     "the first screen stream must reserve its quota");
    passed &= expect(!registry.tryClassify(second, ConnectionPhase::ScreenStream),
                     "a second screen stream must exceed the configured quota");
    passed &= expect(registry.tryClassify(second, ConnectionPhase::OneShot),
                     "a failed quota attempt must leave the connection classifiable");
    passed &= expect(registry.tryClassify(third, ConnectionPhase::ControlStream),
                     "the first control stream must reserve its quota");

    ConnectionPhase previousPhase{ConnectionPhase::Closed};
    passed &= expect(first->state.tryBeginClosing(&previousPhase),
                     "the registered screen stream must begin closing");
    registry.remove(first, previousPhase);
    first->state.markClosed();
    passed &= expect(registry.size() == 3, "removal must erase exactly one registry entry");
    passed &= expect(registry.tryClassify(fourth, ConnectionPhase::ScreenStream),
                     "removal must release the screen-stream quota");
    return passed;
}

}  // namespace

/**
 * @brief Runs connection state-machine and registry invariant tests.
 * @param argc Process argument count.
 * @param argv Process argument values.
 * @return EXIT_SUCCESS when every invariant holds; otherwise EXIT_FAILURE.
 */
int main(int argc, char* argv[])
{
    QCoreApplication const application{argc, argv};
    bool const passed{testStateTransitions() && testConnectionRegistry()};
    std::cout << (passed ? "CONNECTION STATE TESTS PASSED" : "CONNECTION STATE TESTS FAILED")
              << std::endl;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
