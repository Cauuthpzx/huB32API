/**
 * @file ScreenLock.cpp
 * @brief Implementation of the screen lock feature handler.
 *
 * Creates a fullscreen, topmost, black overlay window on a dedicated thread
 * with its own Win32 message loop. The overlay covers all monitors and
 * displays centered lock text. Optionally blocks input via BlockInput().
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "hub32agent/features/ScreenLock.hpp"

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace hub32agent::features {

/// @brief Lock message displayed on the overlay window.
static constexpr const char* kLockMessage = "This computer is locked by the teacher";

/// @brief Window class name for the lock overlay.
static constexpr const char* kWindowClassName = "Hub32LockScreen";

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

/**
 * @brief Constructs the ScreenLock handler in unlocked state.
 */
ScreenLock::ScreenLock() = default;

/**
 * @brief Destroys the ScreenLock handler, ensuring cleanup.
 *
 * If the lock is still active, destroys the overlay window and joins
 * the message loop thread.
 */
ScreenLock::~ScreenLock()
{
    destroyLock();
}

// ---------------------------------------------------------------------------
// FeatureHandler interface
// ---------------------------------------------------------------------------

/**
 * @brief Returns the feature UID used by the server to route commands.
 * @return "lock-screen"
 */
std::string ScreenLock::featureUid() const
{
    return "lock-screen";
}

/**
 * @brief Returns the human-readable feature name for logging.
 * @return "Screen Lock"
 */
std::string ScreenLock::name() const
{
    return "Screen Lock";
}

/**
 * @brief Executes a screen lock operation.
 *
 * For "start": Creates the fullscreen lock overlay on a dedicated thread,
 *   optionally blocks input, and returns {"status":"locked"}.
 * For "stop": Destroys the overlay, unblocks input, and returns {"status":"unlocked"}.
 *
 * @param operation The operation to perform.
 * @param args      Additional arguments (unused).
 * @return JSON result string.
 * @throws std::runtime_error On unknown operation.
 */
std::string ScreenLock::execute(const std::string& operation,
                                 const std::map<std::string, std::string>& /*args*/)
{
    if (operation == "start") {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_locked) {
            spdlog::info("[ScreenLock] already locked, ignoring start");
            return R"({"status":"locked"})";
        }

        m_windowReady = false;

        // Launch the lock window on a dedicated thread
        m_lockThread = std::thread(&ScreenLock::lockThreadFunc, this);

        // Wait for the window to be created (up to 5 seconds)
        for (int i = 0; i < 500 && !m_windowReady.load(); ++i) {
            Sleep(10);
        }

        if (!m_lockHwnd) {
            spdlog::error("[ScreenLock] lock window was not created");
            if (m_lockThread.joinable()) {
                m_lockThread.join();
            }
            throw std::runtime_error("Failed to create lock screen window");
        }

        // Optionally block input (requires admin privileges)
        if (BlockInput(TRUE)) {
            m_inputBlocked = true;
            spdlog::info("[ScreenLock] BlockInput(TRUE) succeeded");
        } else {
            m_inputBlocked = false;
            spdlog::info("[ScreenLock] BlockInput(TRUE) failed (error {}), "
                         "continuing without input blocking",
                         GetLastError());
        }

        m_locked = true;
        spdlog::info("[ScreenLock] screen locked");
        return R"({"status":"locked"})";

    } else if (operation == "stop") {
        destroyLock();
        return R"({"status":"unlocked"})";

    } else {
        throw std::runtime_error("Unknown operation: " + operation);
    }
}

// ---------------------------------------------------------------------------
// Lock thread function
// ---------------------------------------------------------------------------

/**
 * @brief Thread function that creates the lock window and runs its message loop.
 *
 * Registers the "Hub32LockScreen" window class with a black background brush,
 * creates a fullscreen topmost popup window spanning all monitors, shows it,
 * and enters a standard Win32 message loop. The loop exits when WM_CLOSE is
 * received (which calls PostQuitMessage).
 */
void ScreenLock::lockThreadFunc()
{
    // Register window class
    WNDCLASSA wc{};
    wc.lpfnWndProc   = ScreenLock::wndProc;
    wc.hInstance      = GetModuleHandleA(nullptr);
    wc.lpszClassName  = kWindowClassName;
    wc.hbrBackground  = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.hCursor        = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)); // IDC_ARROW

    RegisterClassA(&wc);

    // Get virtual screen dimensions (covers all monitors)
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    spdlog::debug("[ScreenLock] virtual screen: {}x{} at ({},{})", w, h, x, y);

    // Create fullscreen topmost popup window
    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWindowClassName,
        "Hub32 Lock Screen",
        WS_POPUP,
        x, y, w, h,
        nullptr,        // no parent
        nullptr,        // no menu
        GetModuleHandleA(nullptr),
        nullptr         // no create param
    );

    if (!hwnd) {
        spdlog::error("[ScreenLock] CreateWindowExA failed (error {})", GetLastError());
        m_windowReady = true;
        return;
    }

    // Store the window handle and signal readiness
    m_lockHwnd = hwnd;
    m_windowReady = true;

    // Show and activate the lock window
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);

    spdlog::debug("[ScreenLock] lock window created, entering message loop");

    // Message loop
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup: destroy window and unregister class
    DestroyWindow(hwnd);
    UnregisterClassA(kWindowClassName, GetModuleHandleA(nullptr));

    spdlog::debug("[ScreenLock] message loop exited, window destroyed");
}

// ---------------------------------------------------------------------------
// Lock destruction
// ---------------------------------------------------------------------------

/**
 * @brief Destroys the lock window and joins the message loop thread.
 *
 * Thread-safe. Posts WM_CLOSE to the lock window to exit its message loop,
 * joins the thread, and calls BlockInput(FALSE) if input was blocked.
 */
void ScreenLock::destroyLock()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_locked) {
        return;
    }

    // Release input blocking first
    if (m_inputBlocked) {
        BlockInput(FALSE);
        m_inputBlocked = false;
        spdlog::info("[ScreenLock] BlockInput(FALSE) called");
    }

    // Post WM_CLOSE to the lock window to exit the message loop
    if (m_lockHwnd) {
        PostMessage(m_lockHwnd, WM_CLOSE, 0, 0);
        m_lockHwnd = nullptr;
    }

    // Join the message loop thread
    if (m_lockThread.joinable()) {
        m_lockThread.join();
    }

    m_locked = false;
    spdlog::info("[ScreenLock] screen unlocked");
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

/**
 * @brief Win32 window procedure for the lock overlay window.
 *
 * Handles:
 *   - WM_PAINT: Fills the window with black, draws the lock message
 *     centered in white text using a large (36pt) font.
 *   - WM_CLOSE: Calls PostQuitMessage(0) to exit the message loop.
 *   - WM_ERASEBKGND: Returns 1 to prevent erase flicker (WM_PAINT handles it).
 *
 * @param hwnd   Window handle.
 * @param msg    Message identifier.
 * @param wParam Additional message information.
 * @param lParam Additional message information.
 * @return Message processing result.
 */
LRESULT CALLBACK ScreenLock::wndProc(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        // Fill entire client area with black
        RECT clientRect{};
        GetClientRect(hwnd, &clientRect);
        FillRect(hdc, &clientRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        // Create a large font for the lock message
        HFONT font = CreateFontA(
            36,                     // height (36px ~ 27pt at 96 DPI)
            0,                      // width (auto)
            0, 0,                   // escapement, orientation
            FW_BOLD,                // weight
            FALSE, FALSE, FALSE,    // italic, underline, strikeout
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS,
            "Segoe UI"
        );

        HFONT oldFont = nullptr;
        if (font) {
            oldFont = static_cast<HFONT>(SelectObject(hdc, font));
        }

        // Draw centered white text
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);

        DrawTextA(hdc, kLockMessage, -1, &clientRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Cleanup font
        if (font) {
            SelectObject(hdc, oldFont);
            DeleteObject(font);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        // Prevent flicker; WM_PAINT handles all drawing
        return 1;

    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

} // namespace hub32agent::features
