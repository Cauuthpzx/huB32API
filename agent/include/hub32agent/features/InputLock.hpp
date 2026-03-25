/**
 * @file InputLock.hpp
 * @brief Input lock feature handler that blocks keyboard and mouse input.
 *
 * Uses the Win32 BlockInput() API to prevent all keyboard and mouse input
 * from reaching applications. Requires administrator privileges.
 */

#pragma once

#include "hub32agent/FeatureHandler.hpp"

#include <mutex>

namespace hub32agent::features {

/**
 * @brief Input lock feature that blocks keyboard and mouse input.
 *
 * Uses BlockInput(TRUE) to prevent user input from reaching applications.
 * This requires the calling process to have administrator privileges.
 * If the process lacks the required privileges, the "start" operation
 * throws std::runtime_error with the Win32 error code.
 *
 * Operations:
 *   - "start": Call BlockInput(TRUE) to lock input.
 *   - "stop":  Call BlockInput(FALSE) to unlock input.
 */
class InputLock : public FeatureHandler
{
public:
    /**
     * @brief Constructs the InputLock feature handler.
     *
     * Initializes in unlocked state.
     */
    InputLock() = default;

    /**
     * @brief Destructor. Ensures input is unblocked if still locked.
     */
    ~InputLock() override;

    /**
     * @brief Returns the unique feature identifier.
     * @return "input-lock"
     */
    std::string featureUid() const override;

    /**
     * @brief Returns the human-readable feature name.
     * @return "Input Lock"
     */
    std::string name() const override;

    /**
     * @brief Executes an input lock operation.
     *
     * For "start": Calls BlockInput(TRUE). Throws if the call fails
     *   (e.g., insufficient privileges).
     * For "stop": Calls BlockInput(FALSE) to restore input.
     *
     * @param operation The operation to perform ("start" or "stop").
     * @param args      Additional arguments (unused).
     * @return JSON result string: {"status":"locked"} or {"status":"unlocked"}.
     * @throws std::runtime_error If BlockInput() fails or operation is unknown.
     */
    std::string execute(const std::string& operation,
                        const std::map<std::string, std::string>& args) override;

private:
    /// @brief Mutex protecting mutable state.
    std::mutex m_mutex;

    /// @brief Whether input is currently blocked.
    bool m_locked = false;
};

} // namespace hub32agent::features
