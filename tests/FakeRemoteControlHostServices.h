#pragma once

class RemoteControlHostServices;

/**
 * @brief Returns the process-local inert host-services test double.
 * @return Host-services instance that remains valid until process shutdown.
 */
RemoteControlHostServices& fakeRemoteControlHostServices();
