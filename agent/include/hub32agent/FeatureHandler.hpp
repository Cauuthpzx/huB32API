/**
 * @file FeatureHandler.hpp
 * @brief Abstract interface for agent feature handlers.
 *
 * Each feature (screen-capture, lock-screen, etc.) implements this
 * interface. The CommandDispatcher routes commands to the appropriate
 * handler based on the featureUid.
 */

#pragma once

#include <string>
#include <map>

namespace hub32agent {

/**
 * @brief Abstract interface for agent feature handlers.
 *
 * Concrete implementations handle specific operations for a given
 * feature (e.g., capturing a screenshot, locking/unlocking screen).
 * The CommandDispatcher looks up handlers by featureUid and delegates
 * command execution to them.
 */
class FeatureHandler
{
public:
    /**
     * @brief Virtual destructor for proper polymorphic cleanup.
     */
    virtual ~FeatureHandler() = default;

    /**
     * @brief Returns the unique feature identifier.
     *
     * This string must match the featureUid values sent by the server
     * in PendingCommand. Examples: "screen-capture", "lock-screen".
     *
     * @return Feature UID string.
     */
    virtual std::string featureUid() const = 0;

    /**
     * @brief Returns the human-readable feature name.
     *
     * Used for logging and diagnostics.
     *
     * @return Feature name (e.g., "Screen Capture").
     */
    virtual std::string name() const = 0;

    /**
     * @brief Executes a feature operation.
     *
     * Called by the CommandDispatcher when a command targets this feature.
     * Implementations should perform the requested operation and return
     * a JSON result string on success, or throw std::runtime_error on failure.
     *
     * @param operation The operation to perform (e.g., "start", "stop").
     * @param args      Additional arguments for the operation.
     * @return JSON result string on success.
     * @throws std::runtime_error on failure.
     */
    virtual std::string execute(const std::string& operation,
                                const std::map<std::string, std::string>& args) = 0;
};

} // namespace hub32agent
