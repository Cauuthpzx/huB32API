/**
 * @file MessageDisplay.hpp
 * @brief Message display feature handler that shows modal message boxes.
 *
 * Displays a Win32 MessageBoxW to the user on a dedicated thread (since
 * MessageBox blocks until the user dismisses it). The message box is
 * topmost and set to the foreground to ensure visibility.
 */

#pragma once

#include "hub32agent/FeatureHandler.hpp"

#include <mutex>
#include <string>
#include <thread>

namespace hub32agent::features {

/**
 * @brief Message display feature that shows modal message boxes to the user.
 *
 * When activated via "start", launches a MessageBoxW on a separate thread
 * so that the agent's main thread is not blocked. The message box uses
 * MB_TOPMOST and MB_SETFOREGROUND flags to ensure visibility.
 *
 * When "stop" is called, attempts to find and close the message box window
 * by its title text, then joins the thread.
 *
 * Operations:
 *   - "start": Show a message box with title and text from args.
 *   - "stop":  Find and close the message box if still open.
 */
class MessageDisplay : public FeatureHandler
{
public:
    /**
     * @brief Constructs the MessageDisplay feature handler.
     */
    MessageDisplay() = default;

    /**
     * @brief Destructor. Closes any open message box and joins the thread.
     */
    ~MessageDisplay() override;

    /**
     * @brief Returns the unique feature identifier.
     * @return "message-display"
     */
    std::string featureUid() const override;

    /**
     * @brief Returns the human-readable feature name.
     * @return "Message Display"
     */
    std::string name() const override;

    /**
     * @brief Executes a message display operation.
     *
     * For "start":
     *   - Extracts args["title"] (default: "Hub32 Message") and args["text"] (required).
     *   - Converts strings to wide (UTF-16) via MultiByteToWideChar.
     *   - Launches MessageBoxW on a separate thread.
     *
     * For "stop":
     *   - Finds the message box window by title using FindWindowW.
     *   - Posts WM_CLOSE to dismiss it.
     *   - Joins the message thread.
     *
     * @param operation The operation to perform ("start" or "stop").
     * @param args      Arguments: "title" (optional), "text" (required for start).
     * @return JSON result string.
     * @throws std::runtime_error If "text" is missing on start or operation is unknown.
     */
    std::string execute(const std::string& operation,
                        const std::map<std::string, std::string>& args) override;

private:
    /**
     * @brief Converts a UTF-8 string to a UTF-16 wide string.
     *
     * Uses MultiByteToWideChar(CP_UTF8, ...) for the conversion.
     *
     * @param utf8 The UTF-8 encoded input string.
     * @return The UTF-16 wide string.
     */
    static std::wstring utf8ToWide(const std::string& utf8);

    /**
     * @brief Closes any currently-open message box and joins the thread.
     *
     * Attempts to find the message box by its wide title, posts WM_CLOSE,
     * and joins the message thread if joinable.
     */
    void closeMessageBox();

    /// @brief Mutex protecting mutable state.
    std::mutex m_mutex;

    /// @brief Thread running the blocking MessageBoxW call.
    std::thread m_msgThread;

    /// @brief The title of the currently-displayed message box (UTF-8).
    std::string m_currentTitle;

    /// @brief The text of the currently-displayed message box (UTF-8).
    std::string m_currentText;

    /// @brief The wide (UTF-16) title of the currently-displayed message box.
    std::wstring m_currentWideTitle;

    /// @brief Whether a message box is currently being displayed.
    bool m_active = false;
};

} // namespace hub32agent::features
