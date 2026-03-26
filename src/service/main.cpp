#include "../core/PrecompiledHeader.hpp"
#include "WinServiceAdapter.hpp"
#include "../server/HttpServer.hpp"
#include "hub32api/config/ServerConfig.hpp"

#include <csignal>
#include <iostream>
#include <thread>
#include <filesystem>
#include <fstream>

namespace {

/// @brief Async-signal-safe stop flag set by the signal handler.
static volatile sig_atomic_t g_stopRequested = 0;

/// @brief Config reload flag — set when config file mtime changes.
static volatile sig_atomic_t g_reloadRequested = 0;

/// @brief Path to the active config file (empty if loaded from registry).
static std::string g_configPath;

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

    // Watcher thread: polls g_stopRequested and monitors config file for hot-reload
    std::thread watcher([&server] {
        namespace fs = std::filesystem;
        fs::file_time_type lastMtime{};

        // Capture initial mtime of config file
        if (!g_configPath.empty()) {
            std::error_code ec;
            lastMtime = fs::last_write_time(g_configPath, ec);
            if (ec) lastMtime = fs::file_time_type{};
        }

        while (!g_stopRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // Check config file for changes every ~5 seconds (25 iterations of 200ms)
            static int checkCounter = 0;
            if (!g_configPath.empty() && ++checkCounter % 25 == 0) {
                std::error_code ec;
                auto currentMtime = fs::last_write_time(g_configPath, ec);
                if (!ec && currentMtime != lastMtime && lastMtime != fs::file_time_type{}) {
                    lastMtime = currentMtime;
                    spdlog::info("[main] config file '{}' changed — reload available on next restart",
                                 g_configPath);
                    // NOTE: Full hot-reload would require reconstructing HttpServer.
                    // For now, we reload the log level as a safe runtime change.
                    try {
                        auto newCfgResult = hub32api::ServerConfig::from_file(g_configPath);
                        if (newCfgResult.is_err()) {
                            spdlog::warn("[main] config reload failed: {}",
                                         newCfgResult.error().message);
                        } else {
                            auto newCfg = newCfgResult.take();
                            auto lvl = spdlog::level::from_str(newCfg.logLevel);
                            if (lvl != spdlog::get_level()) {
                                spdlog::set_level(lvl);
                                spdlog::info("[main] log level hot-reloaded to '{}'", newCfg.logLevel);
                            }
                        }
                    } catch (const std::exception& ex) {
                        spdlog::warn("[main] config reload failed: {}", ex.what());
                    }
                } else if (!ec && lastMtime == fs::file_time_type{}) {
                    lastMtime = currentMtime;  // First successful read
                }
            }
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
    g_configPath = configPath;
    auto cfgResult = configPath.empty()
        ? hub32api::ServerConfig::from_registry()
        : hub32api::ServerConfig::from_file(configPath);

    if (cfgResult.is_err()) {
        spdlog::critical("[main] configuration error: {}", cfgResult.error().message);
        return 1;
    }
    auto cfg = cfgResult.take();

    if (asService) {
        return hub32api::WinServiceAdapter::runAsService([&cfg]{ return runServer(cfg); });
    }

    // Console mode (--console)
    spdlog::info("[main] running in console mode");
    return runServer(cfg);
}
