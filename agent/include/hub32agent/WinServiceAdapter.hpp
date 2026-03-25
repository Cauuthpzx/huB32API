/**
 * @file WinServiceAdapter.hpp
 * @brief Windows Service adapter for the hub32 agent process.
 *
 * Wraps the Win32 Service Control Manager (SCM) APIs to allow
 * hub32agent to run as an auto-start Windows service. All public
 * methods are static because a single process can host at most
 * one service entry point.
 *
 * Follows the same pattern as hub32api::WinServiceAdapter.
 */

#pragma once

#include <string>
#include <functional>

#ifdef _WIN32
#  include <windows.h>
#  include <winsvc.h>
#endif

namespace hub32agent {

/**
 * @brief Windows Service lifecycle adapter for hub32agent.
 *
 * Provides three capabilities:
 *  - Running the current process as a Windows service (runAsService).
 *  - Installing the service in the SCM (install).
 *  - Removing the service from the SCM (uninstall).
 */
class WinServiceAdapter
{
public:
    /// @brief Callback type for the main work function.
    using WorkFn = std::function<int()>;

    /**
     * @brief Registers with the SCM and dispatches to ServiceMain.
     * @param work Blocking callback that performs agent work.
     * @return 0 on success, 1 on failure.
     */
    static int runAsService(WorkFn work);

    /**
     * @brief Installs hub32agent as a Windows service.
     * @param configPath Optional path to config file (may be empty).
     * @return @c true on success, @c false on failure.
     */
    static bool install(const std::string& configPath = {});

    /**
     * @brief Removes the hub32agent service from the SCM.
     * @return @c true on success, @c false on failure.
     */
    static bool uninstall();

    /**
     * @brief Reports the current service status to the SCM.
     * @param state     Current service state constant.
     * @param exitCode  Win32 exit code (default NO_ERROR).
     * @param waitHint  Estimated pending time in ms (default 0).
     */
    static void reportStatus(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0);

private:
    /// @brief ANSI service name registered with the SCM.
    static constexpr const char* SERVICE_NAME = "hub32agent";

    /// @brief Human-readable display name shown in services.msc.
    static constexpr const char* DISPLAY_NAME = "Hub32 Agent Service";

    /// @brief Description string set via ChangeServiceConfig2A.
    static constexpr const char* DESCRIPTION  = "Hub32 classroom management agent";

    /**
     * @brief ServiceMain entry point called by the SCM dispatcher.
     * @param argc Argument count.
     * @param argv Argument vector (ANSI).
     */
    static void WINAPI ServiceMain(DWORD argc, LPSTR* argv);

    /**
     * @brief Extended control handler registered via RegisterServiceCtrlHandlerExA.
     * @param ctrl       Control code.
     * @param eventType  Event type for extended controls.
     * @param eventData  Event-specific data.
     * @param context    User-defined context pointer.
     * @return NO_ERROR on handled codes, ERROR_CALL_NOT_IMPLEMENTED otherwise.
     */
    static DWORD WINAPI ServiceCtrlHandler(
        DWORD ctrl, DWORD eventType, LPVOID eventData, LPVOID context);

    static SERVICE_STATUS_HANDLE s_statusHandle;  ///< SCM status handle.
    static DWORD                 s_currentState;  ///< Last-reported service state.
    static WorkFn                s_work;          ///< Stored work callback.
};

} // namespace hub32agent
