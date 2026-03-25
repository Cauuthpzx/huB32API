#include "../core/PrecompiledHeader.hpp"
#include "WinServiceAdapter.hpp"

namespace veyon32api {

SERVICE_STATUS_HANDLE WinServiceAdapter::s_statusHandle = nullptr;
DWORD                 WinServiceAdapter::s_currentState = SERVICE_STOPPED;
WinServiceAdapter::WorkFn WinServiceAdapter::s_work;

int WinServiceAdapter::runAsService(WorkFn work)
{
    s_work = std::move(work);

    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(ServiceName), ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(table)) {
        spdlog::error("[WinServiceAdapter] StartServiceCtrlDispatcherW failed: {}", GetLastError());
        return 1;
    }
    return 0;
}

void WINAPI WinServiceAdapter::ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/)
{
    s_statusHandle = RegisterServiceCtrlHandlerW(ServiceName, ServiceCtrlHandler);
    if (!s_statusHandle) return;

    reportStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    // TODO: load config, create HttpServer, start it
    reportStatus(SERVICE_RUNNING);

    int rc = s_work ? s_work() : 0;

    reportStatus(SERVICE_STOPPED, static_cast<DWORD>(rc));
}

void WINAPI WinServiceAdapter::ServiceCtrlHandler(DWORD ctrl)
{
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        reportStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        // TODO: HttpServer::stop()
        reportStatus(SERVICE_STOPPED);
        break;
    default:
        break;
    }
}

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

bool WinServiceAdapter::install(const std::string& /*configPath*/)
{
    // TODO: OpenSCManager → CreateServiceW with SERVICE_WIN32_OWN_PROCESS
    return false;
}

bool WinServiceAdapter::uninstall()
{
    // TODO: OpenSCManager → OpenServiceW → DeleteService
    return false;
}

} // namespace veyon32api
