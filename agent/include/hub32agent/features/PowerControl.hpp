/**
 * @file PowerControl.hpp
 * @brief Power control feature handler for shutdown, reboot, and logoff.
 *
 * Provides system power management operations using the Win32
 * ExitWindowsEx API. Automatically enables the SE_SHUTDOWN_NAME
 * privilege before executing power actions.
 */

#pragma once

#include "hub32agent/FeatureHandler.hpp"

namespace hub32agent::features {

/**
 * @brief Power control feature for shutdown, reboot, and logoff operations.
 *
 * Executes system power actions via ExitWindowsEx(). Before calling
 * ExitWindowsEx, the handler enables the SE_SHUTDOWN_NAME privilege
 * on the process token. Uses EWX_FORCE to ensure the action proceeds
 * even if applications are unresponsive.
 *
 * Operations:
 *   - "start": Execute the power action specified by args["action"].
 *     - "shutdown": Shuts down the computer.
 *     - "reboot":   Restarts the computer.
 *     - "logoff":   Logs off the current user.
 *   - "stop": No-op (cannot undo a power action).
 */
class PowerControl : public FeatureHandler
{
public:
    /**
     * @brief Constructs the PowerControl feature handler.
     */
    PowerControl() = default;

    /**
     * @brief Destructor.
     */
    ~PowerControl() override = default;

    /**
     * @brief Returns the unique feature identifier.
     * @return "power-control"
     */
    std::string featureUid() const override;

    /**
     * @brief Returns the human-readable feature name.
     * @return "Power Control"
     */
    std::string name() const override;

    /**
     * @brief Executes a power control operation.
     *
     * For "start": Enables shutdown privilege and calls ExitWindowsEx
     *   with the appropriate flags based on args["action"].
     * For "stop": Returns {"status":"ok"} (no-op).
     *
     * @param operation The operation to perform ("start" or "stop").
     * @param args      Arguments: "action" (required for start: "shutdown", "reboot", or "logoff").
     * @return JSON result string.
     * @throws std::runtime_error If action is missing/unknown, privilege elevation fails,
     *         or ExitWindowsEx fails.
     */
    std::string execute(const std::string& operation,
                        const std::map<std::string, std::string>& args) override;

private:
    /**
     * @brief Enables the SE_SHUTDOWN_NAME privilege on the current process token.
     *
     * Opens the process token, looks up the shutdown privilege LUID,
     * and adjusts the token privileges to enable it. This is required
     * before calling ExitWindowsEx for shutdown or reboot operations.
     *
     * @throws std::runtime_error If privilege elevation fails.
     */
    static void enableShutdownPrivilege();
};

} // namespace hub32agent::features
