/**
 * @file WinServiceAdapter.cpp
 * @brief Full implementation of the Windows Service adapter for hub32api.
 *
 * Provides service lifecycle management: registering with the SCM,
 * handling control requests, and install/uninstall operations.
 */

#include "../core/PrecompiledHeader.hpp"
#include "WinServiceAdapter.hpp"
#include <csignal>
#include <functional>

namespace hub32api {

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------
SERVICE_STATUS_HANDLE WinServiceAdapter::s_statusHandle = nullptr;
DWORD                 WinServiceAdapter::s_currentState = SERVICE_STOPPED;
WinServiceAdapter::WorkFn WinServiceAdapter::s_work;

/**
 * @brief Static stop callback invoked by ServiceCtrlHandler to stop the server.
 *
 * Set by runAsService() before dispatching; called on SERVICE_CONTROL_STOP
 * and SERVICE_CONTROL_SHUTDOWN to trigger graceful server shutdown.
 */
static std::function<void()> s_stopFn;

/// @brief ANSI service name used for SCM registration and install/uninstall.
static constexpr const char* k_serviceNameA   = "hub32api";

/// @brief Human-readable display name shown in services.msc.
static constexpr const char* k_displayNameA   = "Hub32 API Service";

/// @brief Description string set via ChangeServiceConfig2A.
static constexpr const char* k_descriptionStr = "Hub32 API REST server for remote classroom management.";

// ---------------------------------------------------------------------------
// runAsService
// ---------------------------------------------------------------------------

/**
 * @brief Registers the process with the SCM and dispatches to ServiceMain.
 *
 * Stores @p work in a static so that ServiceMain can invoke it once the
 * service enters the SERVICE_RUNNING state.  Uses StartServiceCtrlDispatcherA
 * with the ANSI service name "hub32api".
 *
 * @param work  Callback that performs the actual server work.  Must block
 *              until the service should stop.
 * @return 0 on success, 1 if the dispatcher fails.
 */
int WinServiceAdapter::runAsService(WorkFn work)
{
    s_work = std::move(work);

    // Register a stop callback that raises SIGTERM.  The signal handler
    // in main.cpp sets g_stopRequested, and the watcher thread calls
    // HttpServer::stop() to unblock the listen() loop.
    s_stopFn = [] { std::raise(SIGTERM); };

    SERVICE_TABLE_ENTRYA table[] = {
        { const_cast<LPSTR>(k_serviceNameA), ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherA(table)) {
        spdlog::error("[WinServiceAdapter] StartServiceCtrlDispatcherA failed: {}",
                      GetLastError());
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// ServiceMain (ANSI overload used by the dispatcher table)
// ---------------------------------------------------------------------------

/**
 * @brief Entry point called by the SCM after StartServiceCtrlDispatcherA.
 *
 * Registers the control handler, transitions to SERVICE_RUNNING, invokes
 * the stored work callback, then reports SERVICE_STOPPED on return.
 *
 * @param argc  Argument count (unused).
 * @param argv  Argument vector (unused).
 */
void WINAPI WinServiceAdapter::ServiceMain(DWORD /*argc*/, LPSTR* /*argv*/)
{
    s_statusHandle = RegisterServiceCtrlHandlerExA(
        k_serviceNameA, ServiceCtrlHandler, nullptr);

    if (!s_statusHandle) {
        spdlog::error("[WinServiceAdapter] RegisterServiceCtrlHandlerExA failed: {}",
                      GetLastError());
        return;
    }

    reportStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    reportStatus(SERVICE_RUNNING);

    int rc = s_work ? s_work() : 0;

    reportStatus(SERVICE_STOPPED, static_cast<DWORD>(rc));
}

// ---------------------------------------------------------------------------
// ServiceCtrlHandler
// ---------------------------------------------------------------------------

/**
 * @brief Handles control requests from the SCM (stop, shutdown, etc.).
 *
 * On SERVICE_CONTROL_STOP or SERVICE_CONTROL_SHUTDOWN: reports
 * STOP_PENDING and invokes the registered stop callback to trigger
 * graceful server shutdown.  The actual SERVICE_STOPPED report is
 * deferred to ServiceMain after the work callback returns.
 *
 * @param ctrl       Control code sent by the SCM.
 * @param eventType  Event type (unused).
 * @param eventData  Event data (unused).
 * @param context    User context (unused).
 * @return NO_ERROR on handled codes, ERROR_CALL_NOT_IMPLEMENTED otherwise.
 */
DWORD WINAPI WinServiceAdapter::ServiceCtrlHandler(
    DWORD ctrl, DWORD /*eventType*/, LPVOID /*eventData*/, LPVOID /*context*/)
{
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        reportStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        // Invoke the stop callback to trigger graceful server shutdown.
        // SERVICE_STOPPED is reported in ServiceMain after s_work returns.
        if (s_stopFn) {
            s_stopFn();
        }
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;

    default:
        break;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

// ---------------------------------------------------------------------------
// reportStatus
// ---------------------------------------------------------------------------

/**
 * @brief Reports the current service status to the SCM.
 *
 * Automatically manages the checkpoint counter for pending states and
 * sets accepted controls based on whether the service is running.
 *
 * @param state     The current service state (e.g. SERVICE_RUNNING).
 * @param exitCode  Win32 exit code; NO_ERROR on normal operation.
 * @param waitHint  Estimated time for a pending operation, in milliseconds.
 */
void WinServiceAdapter::reportStatus(DWORD state, DWORD exitCode, DWORD waitHint)
{
    static DWORD checkPoint = 1;
    s_currentState = state;

    SERVICE_STATUS ss{};
    ss.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    ss.dwCurrentState            = state;
    ss.dwControlsAccepted        = (state == SERVICE_RUNNING)
                                   ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN
                                   : 0;
    ss.dwWin32ExitCode           = exitCode;
    ss.dwWaitHint                = waitHint;
    ss.dwCheckPoint              = (state == SERVICE_RUNNING || state == SERVICE_STOPPED)
                                   ? 0 : checkPoint++;

    SetServiceStatus(s_statusHandle, &ss);
}

// ---------------------------------------------------------------------------
// install
// ---------------------------------------------------------------------------

/**
 * @brief Installs hub32api as a Windows service via the SCM.
 *
 * Retrieves the current executable path with GetModuleFileNameA, builds
 * the command line (optionally including --config), and calls CreateServiceA
 * with SERVICE_AUTO_START.  Sets the service description via
 * ChangeServiceConfig2A.
 *
 * @param configPath  Path to the JSON configuration file.  If empty, the
 *                    service is registered without a --config argument.
 * @return @c true on success, @c false on any failure (logged via spdlog).
 */
bool WinServiceAdapter::install(const std::string& configPath)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        spdlog::error("[WinServiceAdapter] OpenSCManagerA failed: {}", GetLastError());
        return false;
    }

    // Build the full path to the running executable.
    char exePath[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0) {
        spdlog::error("[WinServiceAdapter] GetModuleFileNameA failed: {}", GetLastError());
        CloseServiceHandle(scm);
        return false;
    }

    // Build command line: "<exe>" --config "<configPath>" (or just "<exe>").
    std::string cmdLine;
    if (configPath.empty()) {
        cmdLine = std::string("\"") + exePath + "\"";
    } else {
        cmdLine = std::string("\"") + exePath + "\" --config \"" + configPath + "\"";
    }

    SC_HANDLE svc = CreateServiceA(
        scm,
        k_serviceNameA,
        k_displayNameA,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        cmdLine.c_str(),
        nullptr,   // lpLoadOrderGroup
        nullptr,   // lpdwTagId
        nullptr,   // lpDependencies
        nullptr,   // lpServiceStartName (LocalSystem)
        nullptr);  // lpPassword

    if (!svc) {
        spdlog::error("[WinServiceAdapter] CreateServiceA failed: {}", GetLastError());
        CloseServiceHandle(scm);
        return false;
    }

    // Set the human-readable description shown in services.msc.
    SERVICE_DESCRIPTIONA desc{};
    desc.lpDescription = const_cast<LPSTR>(k_descriptionStr);
    ChangeServiceConfig2A(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    spdlog::info("[WinServiceAdapter] service '{}' installed successfully", k_serviceNameA);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

// ---------------------------------------------------------------------------
// uninstall
// ---------------------------------------------------------------------------

/**
 * @brief Removes the hub32api Windows service from the SCM.
 *
 * Opens the service with DELETE access and calls DeleteService.
 *
 * @return @c true on success, @c false on any failure (logged via spdlog).
 */
bool WinServiceAdapter::uninstall()
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        spdlog::error("[WinServiceAdapter] OpenSCManagerA failed: {}", GetLastError());
        return false;
    }

    SC_HANDLE svc = OpenServiceA(scm, k_serviceNameA, DELETE);
    if (!svc) {
        spdlog::error("[WinServiceAdapter] OpenServiceA failed: {}", GetLastError());
        CloseServiceHandle(scm);
        return false;
    }

    BOOL ok = DeleteService(svc);
    if (!ok) {
        spdlog::error("[WinServiceAdapter] DeleteService failed: {}", GetLastError());
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return false;
    }

    spdlog::info("[WinServiceAdapter] service '{}' uninstalled successfully", k_serviceNameA);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

} // namespace hub32api
