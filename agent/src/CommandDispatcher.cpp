/**
 * @file CommandDispatcher.cpp
 * @brief Implementation of the command dispatcher that routes commands
 *        to registered feature handlers.
 *
 * Maintains an unordered_map of featureUid to FeatureHandler.
 * Dispatching looks up the handler, invokes execute() with timing,
 * and catches any exceptions thrown by the handler.
 */

#include "hub32agent/CommandDispatcher.hpp"
#include "hub32agent/FeatureHandler.hpp"
#include "hub32agent/AgentClient.hpp"  // for PendingCommand

#include <spdlog/spdlog.h>

#include <chrono>

namespace hub32agent {

// ---------------------------------------------------------------------------
// Handler registration
// ---------------------------------------------------------------------------

/**
 * @brief Registers a feature handler.
 *
 * Moves the handler into the internal map, keyed by its featureUid().
 * If a handler with the same featureUid already exists, it is replaced.
 * Logs the registration at info level.
 *
 * @param handler The handler to register. Ownership is transferred.
 */
void CommandDispatcher::registerHandler(std::unique_ptr<FeatureHandler> handler)
{
    if (!handler) {
        spdlog::warn("[Dispatcher] attempted to register null handler");
        return;
    }

    const std::string uid = handler->featureUid();
    const std::string handlerName = handler->name();
    m_handlers[uid] = std::move(handler);

    spdlog::info("[Dispatcher] registered handler: {} ({})", handlerName, uid);
}

// ---------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------

/**
 * @brief Dispatches a command to the appropriate handler.
 *
 * Looks up the handler by cmd.featureUid in the internal map.
 * If found, calls handler->execute() with timing measurements.
 * Catches std::exception and returns failed results with the
 * exception message.
 *
 * @param cmd The command to dispatch.
 * @return DispatchResult with success status, result string, and duration.
 */
DispatchResult CommandDispatcher::dispatch(const PendingCommand& cmd)
{
    auto it = m_handlers.find(cmd.featureUid);
    if (it == m_handlers.end()) {
        spdlog::warn("[Dispatcher] unknown feature: {}", cmd.featureUid);
        return {false, "Unknown feature: " + cmd.featureUid, 0};
    }

    const auto start = std::chrono::steady_clock::now();
    try {
        auto result = it->second->execute(cmd.operation, cmd.arguments);
        const auto end = std::chrono::steady_clock::now();
        const int durationMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

        spdlog::debug("[Dispatcher] command {} executed in {}ms (feature={})",
                      cmd.commandId, durationMs, cmd.featureUid);
        return {true, std::move(result), durationMs};

    } catch (const std::exception& ex) {
        const auto end = std::chrono::steady_clock::now();
        const int durationMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

        spdlog::error("[Dispatcher] feature '{}' threw: {}", cmd.featureUid, ex.what());
        return {false, ex.what(), durationMs};
    }
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

/**
 * @brief Returns list of registered feature UIDs.
 *
 * Collects all keys from the internal handler map into a vector.
 *
 * @return Vector of feature UID strings.
 */
std::vector<std::string> CommandDispatcher::registeredFeatures() const
{
    std::vector<std::string> features;
    features.reserve(m_handlers.size());
    for (const auto& [uid, handler] : m_handlers) {
        features.push_back(uid);
    }
    return features;
}

} // namespace hub32agent
