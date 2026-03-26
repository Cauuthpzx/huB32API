/**
 * @file PowerControl.cpp
 * @brief Implementation of the power control feature handler.
 *
 * Provides shutdown, reboot, and logoff operations using the Win32
 * ExitWindowsEx API. Enables the SE_SHUTDOWN_NAME privilege before
 * executing power actions to ensure they succeed when the process
 * has appropriate access.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "hub32agent/features/PowerControl.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace hub32agent::features {

// ---------------------------------------------------------------------------
// FeatureHandler interface
// ---------------------------------------------------------------------------

/**
 * @brief Returns the feature UID used by the server to route commands.
 * @return "power-control"
 */
std::string PowerControl::featureUid() const
{
    return "power-control";
}

/**
 * @brief Returns the human-readable feature name for logging.
 * @return "Power Control"
 */
std::string PowerControl::name() const
{
    return "Power Control";
}

/**
 * @brief Executes a power control operation.
 *
 * For "start":
 *   - Extracts args["action"] (required: "shutdown", "reboot", or "logoff").
 *   - Enables the SE_SHUTDOWN_NAME privilege.
 *   - Calls ExitWindowsEx with the appropriate flags and EWX_FORCEIFHUNG
 *     (kills only hung applications, not responsive ones).
 *   - Returns {"status":"executing","action":"<action>"}.
 *
 * For "stop":
 *   - No-op; cannot undo a power action.
 *   - Returns {"status":"ok"}.
 *
 * @param operation The operation to perform.
 * @param args      Arguments: "action" required for "start".
 * @return JSON result string.
 * @throws std::runtime_error On missing/unknown action or Win32 API failure.
 */
std::string PowerControl::execute(const std::string& operation,
                                   const std::map<std::string, std::string>& args)
{
    if (operation == "stop") {
        return R"({"status":"ok"})";
    }

    if (operation != "start") {
        throw std::runtime_error("Unknown operation: " + operation);
    }

    // Extract the power action
    std::string action;
    if (auto it = args.find("action"); it != args.end()) {
        action = it->second;
    } else {
        throw std::runtime_error("Missing required argument: 'action'");
    }

    // Determine ExitWindowsEx flags
    UINT flags  = 0;
    DWORD reason = SHTDN_REASON_MAJOR_OTHER;

    if (action == "shutdown") {
        flags = EWX_SHUTDOWN | EWX_FORCEIFHUNG;
    } else if (action == "reboot") {
        flags = EWX_REBOOT | EWX_FORCEIFHUNG;
    } else if (action == "logoff") {
        flags  = EWX_LOGOFF | EWX_FORCEIFHUNG;
        reason = 0;
    } else {
        throw std::runtime_error("Unknown power action: " + action +
                                  " (expected 'shutdown', 'reboot', or 'logoff')");
    }

    // Check for delay argument
    int delaySec = 0;
    if (auto it = args.find("delay"); it != args.end()) {
        try { delaySec = std::clamp(std::stoi(it->second), 0, 300); }
        catch (...) { /* ignore parse errors */ }
    }
    if (delaySec > 0) {
        spdlog::info("[PowerControl] delaying {} seconds before {}", delaySec, action);
        std::this_thread::sleep_for(std::chrono::seconds(delaySec));
    }

    // Enable shutdown privilege (required for shutdown/reboot)
    enableShutdownPrivilege();

    spdlog::info("[PowerControl] executing power action: {}", action);

    // Execute the power action
    if (!ExitWindowsEx(flags, reason)) {
        DWORD err = GetLastError();
        spdlog::error("[PowerControl] ExitWindowsEx failed (error {})", err);
        throw std::runtime_error(
            "ExitWindowsEx failed with error " + std::to_string(err));
    }

    // Build result JSON
    nlohmann::json result;
    result["status"] = "executing";
    result["action"] = action;
    return result.dump();
}

// ---------------------------------------------------------------------------
// Privilege elevation
// ---------------------------------------------------------------------------

/**
 * @brief Enables the SE_SHUTDOWN_NAME privilege on the current process token.
 *
 * Opens the process token with TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
 * looks up the shutdown privilege LUID, and adjusts token privileges
 * to enable it. Logs warnings on failure but does not throw, since
 * logoff may work without this privilege.
 *
 * @throws std::runtime_error If the process token cannot be opened.
 */
void PowerControl::enableShutdownPrivilege()
{
    HANDLE hToken = nullptr;

    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &hToken)) {
        DWORD err = GetLastError();
        spdlog::error("[PowerControl] OpenProcessToken failed (error {})", err);
        throw std::runtime_error(
            "OpenProcessToken failed with error " + std::to_string(err));
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!LookupPrivilegeValueA(nullptr, "SeShutdownPrivilege",
                                &tp.Privileges[0].Luid)) {
        DWORD err = GetLastError();
        CloseHandle(hToken);
        spdlog::warn("[PowerControl] LookupPrivilegeValue failed (error {})", err);
        // Don't throw — logoff may still work without shutdown privilege
        return;
    }

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr)) {
        DWORD err = GetLastError();
        CloseHandle(hToken);
        spdlog::warn("[PowerControl] AdjustTokenPrivileges failed (error {})", err);
        // Don't throw — continue and let ExitWindowsEx report the real error
        return;
    }

    // Check if the privilege was actually enabled
    DWORD lastErr = GetLastError();
    if (lastErr == ERROR_NOT_ALL_ASSIGNED) {
        spdlog::warn("[PowerControl] shutdown privilege not assigned (insufficient rights)");
    } else {
        spdlog::debug("[PowerControl] shutdown privilege enabled");
    }

    CloseHandle(hToken);
}

} // namespace hub32agent::features
