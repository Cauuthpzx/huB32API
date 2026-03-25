/**
 * @file CommandDispatcher.hpp
 * @brief Routes incoming commands to registered feature handlers.
 *
 * The dispatcher maintains a map of featureUid to FeatureHandler.
 * When a command arrives, it looks up the handler and calls execute().
 * Thread-safe for dispatch: handlers are registered at startup only,
 * dispatch is read-only on the handler map.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace hub32agent {

class FeatureHandler;
struct PendingCommand;

/**
 * @brief Result of dispatching a command to a feature handler.
 *
 * Contains the success/failure status, result data or error message,
 * and the execution duration in milliseconds.
 */
struct DispatchResult
{
    bool success = false;   ///< Whether the command executed successfully
    std::string result;     ///< Result data (JSON) or error message
    int durationMs = 0;     ///< Execution duration in milliseconds
};

/**
 * @brief Routes incoming commands to registered feature handlers.
 *
 * Maintains a map of featureUid to FeatureHandler. When a command
 * arrives via dispatch(), the dispatcher looks up the appropriate
 * handler by featureUid and invokes its execute() method.
 *
 * Handlers are registered at startup via registerHandler().
 * After startup, dispatch() performs read-only lookups on the map,
 * making it safe to call from the polling thread.
 */
class CommandDispatcher
{
public:
    /**
     * @brief Default constructor.
     */
    CommandDispatcher() = default;

    /**
     * @brief Registers a feature handler.
     *
     * Transfers ownership of the handler to the dispatcher.
     * The handler is stored by its featureUid(). If a handler with
     * the same featureUid is already registered, it is replaced.
     *
     * @param handler The handler to register. Ownership is transferred.
     */
    void registerHandler(std::unique_ptr<FeatureHandler> handler);

    /**
     * @brief Dispatches a command to the appropriate handler.
     *
     * Looks up the handler by cmd.featureUid. If found, calls
     * handler->execute() with timing. Catches exceptions and
     * returns them as failed results.
     *
     * @param cmd The command to dispatch.
     * @return DispatchResult with success status, result, and duration.
     */
    DispatchResult dispatch(const PendingCommand& cmd);

    /**
     * @brief Returns list of registered feature UIDs.
     *
     * Useful for building the capabilities list during registration.
     *
     * @return Vector of feature UID strings.
     */
    std::vector<std::string> registeredFeatures() const;

private:
    /// @brief Map of featureUid to handler instance.
    std::unordered_map<std::string, std::unique_ptr<FeatureHandler>> m_handlers;
};

} // namespace hub32agent
