#pragma once

#include <string>
#include <functional>

// Windows service infrastructure wrapper.
// Mirrors Hub32's service wrapper pattern.
namespace hub32api {

class WinServiceAdapter
{
public:
    static constexpr const wchar_t* ServiceName = L"hub32api";
    static constexpr const wchar_t* DisplayName = L"Hub3232 API Server";

    using WorkFn = std::function<int()>;

    // Run as Windows service (called from SERVICE_MAIN)
    static int runAsService(WorkFn work);

    // Install/uninstall the service
    static bool install(const std::string& configPath);
    static bool uninstall();

    // Report current status to SCM
    static void reportStatus(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0);

private:
    static void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
    static void WINAPI ServiceCtrlHandler(DWORD ctrl);

    static SERVICE_STATUS_HANDLE s_statusHandle;
    static DWORD                 s_currentState;
    static WorkFn                s_work;
};

} // namespace hub32api
