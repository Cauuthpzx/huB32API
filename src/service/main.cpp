#include "../core/PrecompiledHeader.hpp"
#include "WinServiceAdapter.hpp"
#include "../server/HttpServer.hpp"
#include "hub32api/config/ServerConfig.hpp"

#include <csignal>
#include <iostream>
#include <thread>

namespace {

/// @brief Async-signal-safe stop flag set by the signal handler.
static volatile sig_atomic_t g_stopRequested = 0;

/**
 * @brief Async-signal-safe signal handler.
 *
 * Sets a flag instead of calling non-signal-safe functions like spdlog
 * or mutex-protected methods.  A watcher thread polls this flag and
 * performs the actual graceful shutdown.
 *
 * @param sig The signal number (unused).
 */
void signalHandler(int /*sig*/)
{
    g_stopRequested = 1;
}

/**
 * @brief Creates and runs the HttpServer, with a watcher thread that
 *        polls @c g_stopRequested and calls server.stop() when set.
 *
 * @param cfg The server configuration.
 * @return 0 on success, 1 on failure.
 */
int runServer(const hub32api::ServerConfig& cfg)
{
    hub32api::HttpServer server(cfg);

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Watcher thread: polls g_stopRequested and stops server gracefully
    std::thread watcher([&server] {
        while (!g_stopRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        spdlog::info("[main] stop requested, shutting down server");
        server.stop();
    });

    bool ok = server.start();
    g_stopRequested = 1; // ensure watcher exits if server returns on its own
    watcher.join();
    return ok ? 0 : 1;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // Configure spdlog
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    // Parse --config <path> or --install / --uninstall flags
    std::string configPath;
    bool asService   = true;
    bool doInstall   = false;
    bool doUninstall = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--console" || arg == "-c") asService = false;
        else if (arg == "--install")             doInstall = true;
        else if (arg == "--uninstall")           doUninstall = true;
        else if (arg == "--config" && i+1 < argc)
            configPath = argv[++i];
    }

    if (doInstall)   return hub32api::WinServiceAdapter::install(configPath)   ? 0 : 1;
    if (doUninstall) return hub32api::WinServiceAdapter::uninstall()            ? 0 : 1;

    // Load configuration
    auto cfg = configPath.empty()
        ? hub32api::ServerConfig::from_registry()
        : hub32api::ServerConfig::from_file(configPath);

    if (asService) {
        return hub32api::WinServiceAdapter::runAsService([&cfg]{ return runServer(cfg); });
    }

    // Console mode (--console)
    spdlog::info("[main] running in console mode");
    return runServer(cfg);
}
