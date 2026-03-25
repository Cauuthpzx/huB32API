/**
 * @file main.cpp
 * @brief Entry point for the hub32 agent service binary.
 *
 * Supports running as a Windows service (default), in console mode
 * (--console / -c), or performing install/uninstall operations.
 *
 * The skeleton loop will be replaced by AgentClient + CommandDispatcher
 * in Task 10.
 */

#include <spdlog/spdlog.h>
#include <csignal>
#include <iostream>
#include <thread>

#include "hub32agent/AgentConfig.hpp"
#include "hub32agent/WinServiceAdapter.hpp"

namespace {

/// @brief Async-signal-safe stop flag set by the signal handler.
static volatile sig_atomic_t g_stopRequested = 0;

/**
 * @brief Async-signal-safe signal handler.
 *
 * Sets a flag instead of calling non-signal-safe functions like spdlog
 * or mutex-protected methods. A watcher thread polls this flag and
 * logs the shutdown message.
 *
 * @param sig The signal number (unused).
 */
void signalHandler(int /*sig*/)
{
    g_stopRequested = 1;
}

/**
 * @brief Runs the agent main loop.
 *
 * Installs signal handlers, starts a watcher thread for graceful shutdown,
 * and enters the skeleton polling loop.
 *
 * @param cfg Agent configuration.
 * @return 0 on success, 1 on failure.
 *
 * @note This is a skeleton -- Task 10 will wire in AgentClient,
 *       CommandDispatcher, and all feature handlers.
 */
int runAgent(const hub32agent::AgentConfig& cfg)
{
    spdlog::info("[Agent] starting -- server: {}", cfg.serverUrl);

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Watcher thread for graceful shutdown
    std::thread watcher([] {
        while (!g_stopRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        spdlog::info("[Agent] stop requested, shutting down");
    });

    // Skeleton loop -- will be replaced by AgentClient + CommandDispatcher in Task 10
    while (!g_stopRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    g_stopRequested = 1;
    watcher.join();
    spdlog::info("[Agent] shutdown complete");
    return 0;
}

} // anonymous namespace

/**
 * @brief Agent entry point.
 *
 * Parses command-line arguments and routes to the appropriate action:
 *  - @c --install: Install as a Windows service
 *  - @c --uninstall: Remove the Windows service
 *  - @c --console / @c -c: Run in console (foreground) mode
 *  - @c --config @c \<path\>: Load configuration from a JSON file
 *  - Default (no flags): Run as a Windows service
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on failure.
 */
int main(int argc, char* argv[])
{
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

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

    if (doInstall)   return hub32agent::WinServiceAdapter::install(configPath) ? 0 : 1;
    if (doUninstall) return hub32agent::WinServiceAdapter::uninstall() ? 0 : 1;

    // Load config
    auto cfg = configPath.empty()
        ? hub32agent::AgentConfig::from_registry()
        : hub32agent::AgentConfig::from_file(configPath);

    // Set log level from config
    spdlog::set_level(spdlog::level::from_str(cfg.logLevel));

    if (asService) {
        return hub32agent::WinServiceAdapter::runAsService([&cfg]{ return runAgent(cfg); });
    }

    spdlog::info("[Agent] running in console mode");
    return runAgent(cfg);
}
