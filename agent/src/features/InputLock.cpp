/**
 * @file InputLock.cpp
 * @brief Implementation of the input lock feature handler.
 *
 * Uses the Win32 BlockInput() API to block/unblock all keyboard and
 * mouse input. Requires administrator privileges. If the calling
 * process lacks privileges, BlockInput() fails and this handler
 * throws std::runtime_error with the Win32 error code.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "hub32agent/features/InputLock.hpp"

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace hub32agent::features {

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------

/**
 * @brief Destructor. Unblocks input if still locked.
 *
 * Ensures that input is restored even if the handler is destroyed
 * without an explicit "stop" call.
 */
InputLock::~InputLock()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_locked) {
        BlockInput(FALSE);
        m_locked = false;
        spdlog::info("[InputLock] input unblocked in destructor");
    }
}

// ---------------------------------------------------------------------------
// FeatureHandler interface
// ---------------------------------------------------------------------------

/**
 * @brief Returns the feature UID used by the server to route commands.
 * @return "input-lock"
 */
std::string InputLock::featureUid() const
{
    return "input-lock";
}

/**
 * @brief Returns the human-readable feature name for logging.
 * @return "Input Lock"
 */
std::string InputLock::name() const
{
    return "Input Lock";
}

/**
 * @brief Executes an input lock operation.
 *
 * For "start": Calls BlockInput(TRUE). If the call fails (e.g., due to
 *   insufficient privileges), throws std::runtime_error with the Win32
 *   error code from GetLastError().
 * For "stop": Calls BlockInput(FALSE) to restore input.
 *
 * @param operation The operation to perform.
 * @param args      Additional arguments (unused).
 * @return JSON result string.
 * @throws std::runtime_error On failure or unknown operation.
 */
std::string InputLock::execute(const std::string& operation,
                                const std::map<std::string, std::string>& /*args*/)
{
    if (operation == "start") {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_locked) {
            spdlog::info("[InputLock] already locked, ignoring start");
            return R"({"status":"locked"})";
        }

        if (!BlockInput(TRUE)) {
            DWORD err = GetLastError();
            spdlog::error("[InputLock] BlockInput(TRUE) failed (error {})", err);
            throw std::runtime_error(
                "BlockInput(TRUE) failed with error " + std::to_string(err));
        }

        m_locked = true;
        spdlog::info("[InputLock] input blocked");
        return R"({"status":"locked"})";

    } else if (operation == "stop") {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_locked) {
            BlockInput(FALSE);
            m_locked = false;
            spdlog::info("[InputLock] input unblocked");
        }

        return R"({"status":"unlocked"})";

    } else {
        throw std::runtime_error("Unknown operation: " + operation);
    }
}

} // namespace hub32agent::features
