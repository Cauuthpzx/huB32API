/**
 * @file ScreenCapture.hpp
 * @brief Screen capture feature with tiered capture backends.
 *
 * Provides GPU-accelerated screen capture via DXGI Desktop Duplication
 * (Windows 8+) with automatic fallback to GDI BitBlt for universal
 * compatibility. Captured frames are encoded to JPEG and returned as
 * base64-encoded strings.
 */

#pragma once

#include "hub32agent/FeatureHandler.hpp"
#include <vector>
#include <cstdint>

namespace hub32agent::features {

/**
 * @brief Screen capture feature with tiered capture backends.
 *
 * Capture priority:
 *   1. DXGI Desktop Duplication (Win8+, GPU-accelerated, ~1-2ms)
 *   2. GDI BitBlt fallback (all Windows versions, ~5-15ms)
 *
 * The backend is auto-selected at first capture. If DXGI fails
 * (e.g., RDP session, Win7, GPU unavailable), GDI is used for
 * all subsequent captures.
 *
 * Operations:
 *   - "start": Capture screen, return base64-encoded JPEG
 *   - "stop":  No-op (stateless capture)
 *
 * Arguments:
 *   - "quality": JPEG quality 1-100 (default 75)
 *   - "monitor": Monitor index 0-N (default 0 = primary)
 *   - "width":   Target resize width  (default 0 = original size)
 *   - "height":  Target resize height (default 0 = original size)
 */
class ScreenCapture : public FeatureHandler
{
public:
    /**
     * @brief Constructs the ScreenCapture feature handler.
     *
     * Initializes with DXGI assumed available; actual availability
     * is determined on first capture attempt.
     */
    ScreenCapture();

    /**
     * @brief Destructor. Releases any held DXGI/COM resources.
     */
    ~ScreenCapture() override;

    /**
     * @brief Returns the unique feature identifier.
     * @return "screen-capture"
     */
    std::string featureUid() const override;

    /**
     * @brief Returns the human-readable feature name.
     * @return "Screen Capture"
     */
    std::string name() const override;

    /**
     * @brief Executes a screen capture operation.
     *
     * For "start": captures the screen, encodes to JPEG, returns
     * a JSON object containing the base64-encoded image data along
     * with metadata (format, dimensions, quality, method, size).
     *
     * For "stop": returns a simple OK status (no-op).
     *
     * @param operation The operation to perform ("start" or "stop").
     * @param args      Additional arguments (quality, monitor, width, height).
     * @return JSON result string.
     * @throws std::runtime_error If capture fails on all backends.
     */
    std::string execute(const std::string& operation,
                        const std::map<std::string, std::string>& args) override;

private:
    /**
     * @brief Captures the screen using DXGI Desktop Duplication.
     *
     * Uses Direct3D 11 and DXGI 1.2 to duplicate the desktop output.
     * This is the fastest method, leveraging GPU memory copies, but
     * requires Windows 8+ and a hardware D3D11 device. Fails gracefully
     * under RDP, Hyper-V, or when the GPU is unavailable.
     *
     * @param monitor   Monitor index (0 = primary adapter's first output).
     * @param outWidth  Receives the captured image width in pixels.
     * @param outHeight Receives the captured image height in pixels.
     * @return Raw BGRA pixel data (top-to-bottom), or empty on failure.
     */
    std::vector<uint8_t> captureDxgi(int monitor, int& outWidth, int& outHeight);

    /**
     * @brief Captures the screen using GDI BitBlt.
     *
     * Universal fallback that works on all Windows versions, including
     * RDP sessions. Uses CreateCompatibleBitmap + BitBlt + GetDIBits
     * to obtain raw pixel data.
     *
     * @param monitor   Monitor index (0 = primary). Uses EnumDisplayMonitors
     *                  to resolve multi-monitor coordinates.
     * @param outWidth  Receives the captured image width in pixels.
     * @param outHeight Receives the captured image height in pixels.
     * @return Raw BGRA pixel data (top-to-bottom).
     */
    std::vector<uint8_t> captureGdi(int monitor, int& outWidth, int& outHeight);

    /**
     * @brief Encodes raw BGRA pixels to JPEG format.
     *
     * Converts BGRA pixel data to RGB and encodes using stb_image_write's
     * JPEG encoder.
     *
     * @param pixels  Raw pixel data in BGRA format, top-to-bottom.
     * @param width   Image width in pixels.
     * @param height  Image height in pixels.
     * @param quality JPEG compression quality (1-100).
     * @return JPEG-encoded byte data.
     */
    std::vector<uint8_t> encodeJpeg(const std::vector<uint8_t>& pixels,
                                     int width, int height, int quality);

    /**
     * @brief Encodes binary data to a base64 string.
     *
     * Uses the standard base64 alphabet (A-Z, a-z, 0-9, +, /) with
     * '=' padding.
     *
     * @param data Binary data to encode.
     * @return Base64-encoded string.
     */
    std::string base64Encode(const std::vector<uint8_t>& data);

    /// @brief Whether DXGI Desktop Duplication is considered available.
    /// Set to false after the first DXGI failure to avoid repeated attempts.
    bool m_dxgiAvailable = true;
};

} // namespace hub32agent::features
