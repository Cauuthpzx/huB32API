/**
 * @file MessageDisplay.cpp
 * @brief Implementation of the message display feature handler.
 *
 * Shows a Win32 MessageBoxW on a dedicated thread so the agent's main
 * thread is not blocked. The message box is topmost and brought to
 * the foreground. Supports programmatic dismissal by finding the
 * message box window by title and posting WM_CLOSE.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "hub32agent/features/MessageDisplay.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>

namespace hub32agent::features {

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------

/**
 * @brief Destructor. Closes any open message box and joins the thread.
 */
MessageDisplay::~MessageDisplay()
{
    closeMessageBox();
}

// ---------------------------------------------------------------------------
// FeatureHandler interface
// ---------------------------------------------------------------------------

/**
 * @brief Returns the feature UID used by the server to route commands.
 * @return "message-display"
 */
std::string MessageDisplay::featureUid() const
{
    return "message-display";
}

/**
 * @brief Returns the human-readable feature name for logging.
 * @return "Message Display"
 */
std::string MessageDisplay::name() const
{
    return "Message Display";
}

/**
 * @brief Executes a message display operation.
 *
 * For "start":
 *   - Closes any previously-open message box.
 *   - Extracts "title" (default "Hub32 Message") and "text" (required) from args.
 *   - Converts both to wide strings (UTF-16).
 *   - Launches MessageBoxW on a dedicated thread.
 *   - Returns JSON with status, title, and text.
 *
 * For "stop":
 *   - Finds the message box by its title and posts WM_CLOSE.
 *   - Joins the message thread.
 *
 * @param operation The operation to perform.
 * @param args      Arguments: "title" (optional), "text" (required for start).
 * @return JSON result string.
 * @throws std::runtime_error If "text" is missing or operation is unknown.
 */
std::string MessageDisplay::execute(const std::string& operation,
                                     const std::map<std::string, std::string>& args)
{
    if (operation == "start") {
        // Close any existing message box before showing a new one
        closeMessageBox();

        // Extract arguments
        std::string title = "Hub32 Message";
        if (auto it = args.find("title"); it != args.end()) {
            title = it->second;
        }

        std::string text;
        if (auto it = args.find("text"); it != args.end()) {
            text = it->second;
        } else {
            throw std::runtime_error("Missing required argument: 'text'");
        }

        // Convert to wide strings
        std::wstring wideTitle = utf8ToWide(title);
        std::wstring wideText  = utf8ToWide(text);

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            m_currentTitle     = title;
            m_currentText      = text;
            m_currentWideTitle = wideTitle;
            m_active           = true;

            // Launch MessageBoxW on a separate thread (it blocks until dismissed)
            m_msgThread = std::thread([wideTitle = std::move(wideTitle),
                                       wideText = std::move(wideText)]() {
                MessageBoxW(
                    nullptr,
                    wideText.c_str(),
                    wideTitle.c_str(),
                    MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND
                );
            });
        }

        spdlog::info("[MessageDisplay] showing message: title='{}', text='{}'",
                     title, text);

        // Build result JSON
        nlohmann::json result;
        result["status"] = "shown";
        result["title"]  = title;
        result["text"]   = text;
        return result.dump();

    } else if (operation == "stop") {
        closeMessageBox();
        return R"({"status":"closed"})";

    } else {
        throw std::runtime_error("Unknown operation: " + operation);
    }
}

// ---------------------------------------------------------------------------
// UTF-8 to wide string conversion
// ---------------------------------------------------------------------------

/**
 * @brief Converts a UTF-8 string to a UTF-16 wide string.
 *
 * Uses MultiByteToWideChar(CP_UTF8, ...) for the conversion. Returns
 * an empty wstring if the input is empty or conversion fails.
 *
 * @param utf8 The UTF-8 encoded input string.
 * @return The UTF-16 wide string.
 */
std::wstring MessageDisplay::utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) {
        return {};
    }

    int requiredSize = MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.c_str(), static_cast<int>(utf8.size()),
        nullptr, 0
    );

    if (requiredSize <= 0) {
        spdlog::warn("[MessageDisplay] MultiByteToWideChar size query failed (error {})",
                     GetLastError());
        return {};
    }

    std::wstring wide(static_cast<size_t>(requiredSize), L'\0');
    int written = MultiByteToWideChar(
        CP_UTF8, 0,
        utf8.c_str(), static_cast<int>(utf8.size()),
        wide.data(), requiredSize
    );

    if (written <= 0) {
        spdlog::warn("[MessageDisplay] MultiByteToWideChar conversion failed (error {})",
                     GetLastError());
        return {};
    }

    return wide;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/**
 * @brief Closes any currently-open message box and joins the thread.
 *
 * Finds the message box window by its wide title using FindWindowW,
 * posts WM_CLOSE to dismiss it, and joins the message thread.
 */
void MessageDisplay::closeMessageBox()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_active) {
        return;
    }

    // Try to find and close the message box window by title
    if (!m_currentWideTitle.empty()) {
        HWND hwnd = FindWindowW(nullptr, m_currentWideTitle.c_str());
        if (hwnd) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            spdlog::debug("[MessageDisplay] posted WM_CLOSE to message box");
        } else {
            spdlog::debug("[MessageDisplay] message box window not found (already closed?)");
        }
    }

    // Join the thread
    if (m_msgThread.joinable()) {
        m_msgThread.join();
    }

    m_active = false;
    m_currentTitle.clear();
    m_currentText.clear();
    m_currentWideTitle.clear();

    spdlog::info("[MessageDisplay] message box closed");
}

} // namespace hub32agent::features
