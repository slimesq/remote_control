#include "FakeRemoteControlHostServices.h"

#include "RemoteControlHostServices.h"

namespace
{

/** @brief Provides inert host operations for transport-only tests. */
class FakeRemoteControlHostServices final : public RemoteControlHostServices
{
public:
    /** @return No file-system roots because transport tests do not browse files. */
    [[nodiscard]] QStringList localDriveRoots() const override
    {
        return {};
    }

    /**
     * @brief Rejects test file-system access.
     * @param _path Ignored test path.
     * @return Always false.
     */
    [[nodiscard]] bool isFilePathAllowed(QString const& _path) const override
    {
        static_cast<void>(_path);
        return false;
    }

    /**
     * @brief Rejects test shell operations.
     * @param _path Ignored test path.
     * @return Always false.
     */
    [[nodiscard]] bool openFile(QString const& _path) override
    {
        static_cast<void>(_path);
        return false;
    }

    /**
     * @brief Rejects test input injection.
     * @param _event Ignored test mouse event.
     * @return Always false.
     */
    [[nodiscard]] bool sendMouseEvent(remote_control::MouseEventPacket const& _event) override
    {
        static_cast<void>(_event);
        return false;
    }

    /** @return Empty bytes because transport tests do not capture the screen. */
    [[nodiscard]] QByteArray captureScreenPng() override
    {
        return {};
    }

    /**
     * @brief Rejects test lock-screen operations.
     * @param _locked Ignored test lock state.
     * @return Always false.
     */
    [[nodiscard]] bool requestScreenLock(bool _locked) override
    {
        static_cast<void>(_locked);
        return false;
    }
};

}  // namespace

/**
 * @brief Returns the process-local inert host-services test double.
 * @return Host-services instance that remains valid until process shutdown.
 */
RemoteControlHostServices& fakeRemoteControlHostServices()
{
    static FakeRemoteControlHostServices services;
    return services;
}
