/**
 * @file main.cpp
 * @brief Entry point for the hub32 agent service binary.
 *
 * Supports running as a Windows service (default), in console mode
 * (--console / -c), or performing install/uninstall operations.
 *
 * The agent lifecycle:
 *   1. Load configuration
 *   2. Create CommandDispatcher and register all 5 feature handlers
 *   3. Create AgentClient and register with the server
 *   4. Enter main loop:
 *      a. Poll commands from server
 *      b. Dispatch each command to the appropriate handler
 *      c. Report results back to server
 *      d. Send heartbeat periodically
 *      e. Check for stop signal
 *   5. Unregister from server on shutdown
 */

#include <spdlog/spdlog.h>
#include <csignal>
#include <iostream>
#include <thread>
#include <chrono>

#include "hub32agent/AgentConfig.hpp"
#include "hub32agent/WinServiceAdapter.hpp"
#include "hub32agent/AgentClient.hpp"
#include "hub32agent/CommandDispatcher.hpp"

// Feature handlers
#include "hub32agent/features/ScreenCapture.hpp"
#include "hub32agent/features/ScreenLock.hpp"
#include "hub32agent/features/InputLock.hpp"
#include "hub32agent/features/MessageDisplay.hpp"
#include "hub32agent/features/PowerControl.hpp"

// Streaming pipeline (optional — requires FFmpeg + WebRTC at build time)
#if defined(HUB32_WITH_FFMPEG) && defined(HUB32_WITH_WEBRTC)
#include "hub32agent/pipeline/StreamPipeline.hpp"
#include "hub32agent/webrtc/SignalingClient.hpp"
#include "hub32agent/webrtc/WebRtcProducer.hpp"
#include "hub32agent/features/StreamControl.hpp"
#define HUB32_STREAMING_ENABLED 1
#endif

namespace {

/// @brief Async-signal-safe stop flag set by the signal handler.
static volatile sig_atomic_t g_stopRequested = 0;

/**
 * @brief Async-signal-safe signal handler.
 *
 * Sets a flag instead of calling non-signal-safe functions like spdlog
 * or mutex-protected methods. The main loop polls this flag.
 *
 * @param sig The signal number (unused).
 */
void signalHandler(int /*sig*/)
{
    g_stopRequested = 1;
}

/**
 * @brief Creates a CommandDispatcher and registers all 5 feature handlers.
 * @return Configured CommandDispatcher ready to receive commands.
 */
hub32agent::CommandDispatcher createDispatcher()
{
    hub32agent::CommandDispatcher dispatcher;
    dispatcher.registerHandler(std::make_unique<hub32agent::features::ScreenCapture>());
    dispatcher.registerHandler(std::make_unique<hub32agent::features::ScreenLock>());
    dispatcher.registerHandler(std::make_unique<hub32agent::features::InputLock>());
    dispatcher.registerHandler(std::make_unique<hub32agent::features::MessageDisplay>());
    dispatcher.registerHandler(std::make_unique<hub32agent::features::PowerControl>());
    return dispatcher;
}

/**
 * @brief Runs the agent main loop.
 *
 * 1. Creates CommandDispatcher with all feature handlers
 * 2. Creates AgentClient and registers with the server
 * 3. Enters poll/dispatch/report loop until stop signal
 * 4. Unregisters from server on shutdown
 *
 * @param cfg Agent configuration.
 * @return 0 on success, 1 on failure.
 */
int runAgent(const hub32agent::AgentConfig& cfg)
{
    spdlog::info("[Agent] starting — server: {}", cfg.serverUrl);

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // --- 1. Create dispatcher and register feature handlers ---
    auto dispatcher = createDispatcher();
    {
        const auto features = dispatcher.registeredFeatures();
        spdlog::info("[Agent] {} features registered: {}", features.size(),
                     [&features] {
                         std::string s;
                         for (size_t i = 0; i < features.size(); ++i) {
                             if (i > 0) s += ", ";
                             s += features[i];
                         }
                         return s;
                     }());
    }

    // --- 2. Create client and register with server ---
    hub32agent::AgentClient client(cfg);

    // Retry registration with exponential backoff
    int retryDelay = 1;
    while (!g_stopRequested && !client.isRegistered()) {
        if (client.registerWithServer()) {
            spdlog::info("[Agent] registered as '{}' with server", client.agentId());
            break;
        }
        spdlog::warn("[Agent] registration failed, retrying in {}s", retryDelay);
        for (int i = 0; i < retryDelay * 5 && !g_stopRequested; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        retryDelay = std::min(retryDelay * 2, 60); // max 60s between retries
    }

    if (g_stopRequested) {
        spdlog::info("[Agent] stop requested before registration completed");
        return 0;
    }

    // --- 2b. Initialize streaming pipeline (if built with FFmpeg + WebRTC) ---
#ifdef HUB32_STREAMING_ENABLED
    hub32agent::webrtc::SignalingClient signaling(cfg.serverUrl, client.authToken());
    hub32agent::webrtc::WebRtcProducer::Config producerCfg;
    producerCfg.locationId = cfg.locationId;
    hub32agent::webrtc::WebRtcProducer producer(signaling, producerCfg);

    auto pipeline = std::make_unique<hub32agent::pipeline::StreamPipeline>(producer);

    // Register stream-control feature handler (server can start/stop streaming)
    dispatcher.registerHandler(
        std::make_unique<hub32agent::features::StreamControl>(*pipeline, producer));
    spdlog::info("[Agent] streaming support enabled (server can send stream-control commands)");

    // Auto-start streaming if locationId is configured
    if (!cfg.locationId.empty()) {
        if (producer.connect()) {
            hub32agent::pipeline::PipelineConfig pipeCfg;
            pipeCfg.width  = cfg.streamWidth  > 0 ? cfg.streamWidth  : 1920;
            pipeCfg.height = cfg.streamHeight > 0 ? cfg.streamHeight : 1080;
            pipeCfg.fps    = cfg.streamFps    > 0 ? cfg.streamFps    : 15;

            if (pipeline->start(pipeCfg)) {
                spdlog::info("[Agent] auto-started streaming ({}x{} @{} fps, path={})",
                             pipeCfg.width, pipeCfg.height, pipeCfg.fps,
                             hub32agent::pipeline::to_string(pipeline->activePath()));
            }
        }
    }
#endif

    // --- 3. Main loop: poll → dispatch → report → heartbeat ---
    auto lastHeartbeat = std::chrono::steady_clock::now();
    const auto pollInterval = std::chrono::milliseconds(cfg.pollIntervalMs);
    const auto heartbeatInterval = std::chrono::milliseconds(cfg.heartbeatIntervalMs);

    spdlog::info("[Agent] entering main loop (poll={}ms, heartbeat={}ms)",
                 cfg.pollIntervalMs, cfg.heartbeatIntervalMs);

    while (!g_stopRequested) {
        // --- Poll commands ---
        auto commands = client.pollCommands();

        // --- Dispatch each command ---
        for (const auto& cmd : commands) {
            if (g_stopRequested) break;

            spdlog::info("[Agent] executing command '{}': feature='{}' op='{}'",
                         cmd.commandId, cmd.featureUid, cmd.operation);

            auto result = dispatcher.dispatch(cmd);

            // Report result back to server
            const std::string status = result.success ? "success" : "failed";
            client.reportResult(cmd.commandId, status, result.result, result.durationMs);

            spdlog::info("[Agent] command '{}' {} ({}ms)",
                         cmd.commandId, status, result.durationMs);
        }

        // --- Heartbeat ---
        const auto now = std::chrono::steady_clock::now();
        if (now - lastHeartbeat >= heartbeatInterval) {
            client.sendHeartbeat();
            lastHeartbeat = now;
        }

        // --- Wait for next poll ---
        // Sleep in small increments so we can respond to stop signal quickly
        const auto sleepEnd = std::chrono::steady_clock::now() + pollInterval;
        while (!g_stopRequested && std::chrono::steady_clock::now() < sleepEnd) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    // --- 4. Graceful shutdown ---
#ifdef HUB32_STREAMING_ENABLED
    if (pipeline && pipeline->isRunning()) {
        spdlog::info("[Agent] stopping streaming pipeline");
        pipeline->stop();
    }
    producer.disconnect();
#endif

    spdlog::info("[Agent] shutting down, unregistering from server");
    client.unregister();
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
