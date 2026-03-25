/**
 * @file ScreenLock.hpp
 * @brief Screen lock feature handler that creates a fullscreen blocking overlay.
 *
 * Creates a fullscreen, topmost, black overlay window that covers all monitors
 * and blocks user interaction. The lock window runs on a dedicated thread with
 * its own Win32 message loop. Optionally calls BlockInput() if the agent has
 * administrator privileges.
 */

#pragma once

#include "hub32agent/FeatureHandler.hpp"

#include <atomic>
#include <mutex>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace hub32agent::features {

/**
 * @brief Screen lock feature that creates a fullscreen blocking overlay.
 *
 * When activated via the "start" operation, creates a fullscreen topmost
 * black window spanning all monitors with centered white text reading
 * "This computer is locked by the teacher". The window runs on a dedicated
 * thread with its own Win32 message loop, ensuring it remains responsive
 * and topmost.
 *
 * When deactivated via "stop", the lock window is destroyed by posting
 * WM_CLOSE to its message loop thread, and BlockInput is released if it
 * was enabled.
 *
 * Operations:
 *   - "start": Create the lock overlay window and optionally block input.
 *   - "stop":  Destroy the lock overlay and restore input.
 */
class ScreenLock : public FeatureHandler
{
public:
    /**
     * @brief Constructs the ScreenLock feature handler.
     *
     * Initializes in unlocked state with no active window or thread.
     */
    ScreenLock();

    /**
     * @brief Destructor. Ensures the lock window is destroyed and resources cleaned up.
     */
    ~ScreenLock() override;

    /**
     * @brief Returns the unique feature identifier.
     * @return "lock-screen"
     */
    std::string featureUid() const override;

    /**
     * @brief Returns the human-readable feature name.
     * @return "Screen Lock"
     */
    std::string name() const override;

    /**
     * @brief Executes a screen lock operation.
     *
     * For "start": Creates the fullscreen lock overlay on a separate thread.
     * For "stop": Destroys the lock overlay and joins the thread.
     *
     * @param operation The operation to perform ("start" or "stop").
     * @param args      Additional arguments (unused).
     * @return JSON result string: {"status":"locked"} or {"status":"unlocked"}.
     * @throws std::runtime_error If operation is unknown or lock creation fails.
     */
    std::string execute(const std::string& operation,
                        const std::map<std::string, std::string>& args) override;

private:
    /**
     * @brief Thread function that creates the lock window and runs its message loop.
     *
     * Registers the window class, creates the fullscreen overlay window,
     * and enters a GetMessage/TranslateMessage/DispatchMessage loop until
     * WM_CLOSE is received.
     */
    void lockThreadFunc();

    /**
     * @brief Destroys the lock window and joins the message loop thread.
     *
     * Posts WM_CLOSE to the lock window to exit the message loop, then
     * joins the thread. Also calls BlockInput(FALSE) if input was blocked.
     * Called by "stop" operation and by the destructor.
     */
    void destroyLock();

    /**
     * @brief Win32 window procedure for the lock overlay window.
     *
     * Handles:
     *   - WM_PAINT: Draws centered lock message text in white on black.
     *   - WM_CLOSE: Calls PostQuitMessage(0) to exit the message loop.
     *   - WM_ERASEBKGND: Returns 1 to prevent flicker.
     *
     * @param hwnd   Window handle.
     * @param msg    Message identifier.
     * @param wParam Additional message information.
     * @param lParam Additional message information.
     * @return Message processing result.
     */
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    /// @brief Mutex protecting mutable state (lock window handle, thread, flags).
    std::mutex m_mutex;

    /// @brief Handle to the fullscreen lock overlay window.
    HWND m_lockHwnd = nullptr;

    /// @brief Thread running the lock window's message loop.
    std::thread m_lockThread;

    /// @brief Whether BlockInput(TRUE) was successfully called.
    bool m_inputBlocked = false;

    /// @brief Whether the lock is currently active.
    bool m_locked = false;

    /// @brief Signals that the window has been created on the lock thread.
    std::atomic<bool> m_windowReady{false};
};

} // namespace hub32agent::features
