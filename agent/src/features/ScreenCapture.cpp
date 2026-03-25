/**
 * @file ScreenCapture.cpp
 * @brief Implementation of the tiered screen capture feature.
 *
 * Provides two capture backends:
 *   1. DXGI Desktop Duplication (Win8+, GPU-accelerated)
 *   2. GDI BitBlt (universal fallback)
 *
 * The captured frame is encoded to JPEG via stb_image_write and
 * returned as a base64-encoded string within a JSON envelope.
 *
 * @note This file is compiled under MinGW/GCC. DXGI COM interfaces
 *       use DEFINE_GUID from the system headers; __uuidof() is not
 *       used. The dxguid library provides the GUID definitions.
 */

// -----------------------------------------------------------------------
// stb_image_write implementation (must come before other includes that
// might pull in windows.h, to avoid macro conflicts with stb internals)
// -----------------------------------------------------------------------
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

// -----------------------------------------------------------------------
// Windows + DirectX headers
// -----------------------------------------------------------------------
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

// -----------------------------------------------------------------------
// Standard / project headers
// -----------------------------------------------------------------------
#include "hub32agent/features/ScreenCapture.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hub32agent::features {

// -----------------------------------------------------------------------
// Construction / Destruction
// -----------------------------------------------------------------------

/**
 * @brief Constructs the ScreenCapture handler.
 *
 * DXGI is assumed available until the first capture attempt proves
 * otherwise, at which point the handler permanently falls back to GDI.
 */
ScreenCapture::ScreenCapture() = default;

/**
 * @brief Destructor. No persistent resources to release.
 */
ScreenCapture::~ScreenCapture() = default;

// -----------------------------------------------------------------------
// FeatureHandler interface
// -----------------------------------------------------------------------

/**
 * @brief Returns the feature UID used by the server to route commands.
 * @return "screen-capture"
 */
std::string ScreenCapture::featureUid() const
{
    return "screen-capture";
}

/**
 * @brief Returns the human-readable feature name for logging.
 * @return "Screen Capture"
 */
std::string ScreenCapture::name() const
{
    return "Screen Capture";
}

/**
 * @brief Executes a screen capture operation.
 *
 * Supported operations:
 *   - "start": Capture the screen, encode to JPEG, return base64.
 *   - "stop":  No-op, returns {"status":"ok"}.
 *
 * The JSON result for "start" contains:
 *   - format:    "jpeg"
 *   - width:     Captured image width
 *   - height:    Captured image height
 *   - quality:   JPEG quality used
 *   - method:    "dxgi" or "gdi"
 *   - sizeBytes: JPEG byte count
 *   - image:     Base64-encoded JPEG data
 *
 * @param operation Operation name.
 * @param args      Optional arguments (quality, monitor, width, height).
 * @return JSON result string.
 * @throws std::runtime_error on unknown operation or total capture failure.
 */
std::string ScreenCapture::execute(const std::string& operation,
                                    const std::map<std::string, std::string>& args)
{
    if (operation == "stop") {
        return R"({"status":"ok"})";
    }

    if (operation != "start") {
        throw std::runtime_error("Unknown operation: " + operation);
    }

    // Parse optional arguments
    int quality = 75;
    int monitor = 0;

    if (auto it = args.find("quality"); it != args.end()) {
        quality = std::clamp(std::stoi(it->second), 1, 100);
    }
    if (auto it = args.find("monitor"); it != args.end()) {
        monitor = std::max(0, std::stoi(it->second));
    }

    int width  = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    std::string method;

    // Tier 1: DXGI Desktop Duplication
    if (m_dxgiAvailable) {
        pixels = captureDxgi(monitor, width, height);
        if (!pixels.empty()) {
            method = "dxgi";
        } else {
            m_dxgiAvailable = false;
            spdlog::info("[ScreenCapture] DXGI unavailable, falling back to GDI");
        }
    }

    // Tier 2: GDI BitBlt fallback
    if (pixels.empty()) {
        pixels = captureGdi(monitor, width, height);
        method = "gdi";
    }

    if (pixels.empty()) {
        throw std::runtime_error("Screen capture failed: all backends returned empty data");
    }

    // Encode to JPEG
    auto jpeg = encodeJpeg(pixels, width, height, quality);
    if (jpeg.empty()) {
        throw std::runtime_error("JPEG encoding failed");
    }

    // Encode to base64
    auto b64 = base64Encode(jpeg);

    // Build JSON result
    nlohmann::json result;
    result["format"]    = "jpeg";
    result["width"]     = width;
    result["height"]    = height;
    result["quality"]   = quality;
    result["method"]    = method;
    result["sizeBytes"] = static_cast<int>(jpeg.size());
    result["image"]     = std::move(b64);

    return result.dump();
}

// -----------------------------------------------------------------------
// DXGI Desktop Duplication capture
// -----------------------------------------------------------------------

/**
 * @brief Helper RAII wrapper to ensure COM is initialized/uninitialized
 *        on the calling thread.
 */
namespace {

struct ComScope
{
    HRESULT hr;

    ComScope()
    {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }

    ~ComScope()
    {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }

    bool ok() const { return SUCCEEDED(hr); }
};

} // anonymous namespace

/**
 * @brief Captures the screen via DXGI Desktop Duplication.
 *
 * Steps:
 *   1. Create a D3D11 hardware device.
 *   2. Query for IDXGIDevice, get the adapter.
 *   3. Enumerate outputs on the adapter.
 *   4. Query IDXGIOutput1 and call DuplicateOutput.
 *   5. AcquireNextFrame to get the desktop texture.
 *   6. Copy to a CPU-readable staging texture.
 *   7. Map and read BGRA pixel data.
 *
 * All COM pointers are released manually to avoid CComPtr dependency.
 *
 * @param monitor   Monitor index.
 * @param outWidth  Receives image width.
 * @param outHeight Receives image height.
 * @return BGRA pixel data, or empty vector on failure.
 */
std::vector<uint8_t> ScreenCapture::captureDxgi(int monitor,
                                                  int& outWidth, int& outHeight)
{
    std::vector<uint8_t> result;

    ComScope com;
    if (!com.ok()) {
        spdlog::debug("[ScreenCapture] COM init failed: 0x{:08X}", static_cast<unsigned>(com.hr));
        return result;
    }

    // --- Step 1: Create D3D11 device ---
    ID3D11Device*        device  = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL    featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,                    // no software rasterizer
        0,                          // flags
        nullptr, 0,                 // default feature levels
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context
    );

    if (FAILED(hr) || !device) {
        spdlog::debug("[ScreenCapture] D3D11CreateDevice failed: 0x{:08X}", static_cast<unsigned>(hr));
        return result;
    }

    // --- Step 2: Get DXGI device and adapter ---
    IDXGIDevice* dxgiDevice = nullptr;
    hr = device->QueryInterface(IID_IDXGIDevice, reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(hr) || !dxgiDevice) {
        spdlog::debug("[ScreenCapture] QueryInterface(IDXGIDevice) failed");
        context->Release();
        device->Release();
        return result;
    }

    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();

    if (FAILED(hr) || !adapter) {
        spdlog::debug("[ScreenCapture] GetAdapter failed");
        context->Release();
        device->Release();
        return result;
    }

    // --- Step 3: Enumerate outputs, select requested monitor ---
    IDXGIOutput* output = nullptr;
    hr = adapter->EnumOutputs(static_cast<UINT>(monitor), &output);
    adapter->Release();

    if (FAILED(hr) || !output) {
        spdlog::debug("[ScreenCapture] EnumOutputs({}) failed", monitor);
        context->Release();
        device->Release();
        return result;
    }

    // --- Step 4: QueryInterface for IDXGIOutput1 (requires Win8+) ---
    IDXGIOutput1* output1 = nullptr;
    hr = output->QueryInterface(IID_IDXGIOutput1, reinterpret_cast<void**>(&output1));
    output->Release();

    if (FAILED(hr) || !output1) {
        spdlog::debug("[ScreenCapture] QueryInterface(IDXGIOutput1) failed — pre-Win8?");
        context->Release();
        device->Release();
        return result;
    }

    // --- Step 5: Duplicate output ---
    IDXGIOutputDuplication* duplication = nullptr;
    hr = output1->DuplicateOutput(device, &duplication);
    output1->Release();

    if (FAILED(hr) || !duplication) {
        spdlog::debug("[ScreenCapture] DuplicateOutput failed: 0x{:08X}", static_cast<unsigned>(hr));
        context->Release();
        device->Release();
        return result;
    }

    // --- Step 6: Acquire next frame ---
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    IDXGIResource*          desktopResource = nullptr;
    hr = duplication->AcquireNextFrame(500, &frameInfo, &desktopResource);

    if (FAILED(hr) || !desktopResource) {
        spdlog::debug("[ScreenCapture] AcquireNextFrame failed: 0x{:08X}", static_cast<unsigned>(hr));
        duplication->Release();
        context->Release();
        device->Release();
        return result;
    }

    // --- Step 7: Get the desktop texture ---
    ID3D11Texture2D* desktopTexture = nullptr;
    hr = desktopResource->QueryInterface(IID_ID3D11Texture2D,
                                          reinterpret_cast<void**>(&desktopTexture));
    desktopResource->Release();

    if (FAILED(hr) || !desktopTexture) {
        spdlog::debug("[ScreenCapture] QueryInterface(ID3D11Texture2D) failed");
        duplication->ReleaseFrame();
        duplication->Release();
        context->Release();
        device->Release();
        return result;
    }

    // --- Step 8: Get texture dimensions ---
    D3D11_TEXTURE2D_DESC texDesc{};
    desktopTexture->GetDesc(&texDesc);
    outWidth  = static_cast<int>(texDesc.Width);
    outHeight = static_cast<int>(texDesc.Height);

    // --- Step 9: Create CPU-readable staging texture ---
    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width              = texDesc.Width;
    stagingDesc.Height             = texDesc.Height;
    stagingDesc.MipLevels          = 1;
    stagingDesc.ArraySize          = 1;
    stagingDesc.Format             = texDesc.Format;
    stagingDesc.SampleDesc.Count   = 1;
    stagingDesc.SampleDesc.Quality = 0;
    stagingDesc.Usage              = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags          = 0;
    stagingDesc.MiscFlags          = 0;

    ID3D11Texture2D* stagingTexture = nullptr;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);

    if (FAILED(hr) || !stagingTexture) {
        spdlog::debug("[ScreenCapture] CreateTexture2D (staging) failed");
        desktopTexture->Release();
        duplication->ReleaseFrame();
        duplication->Release();
        context->Release();
        device->Release();
        return result;
    }

    // --- Step 10: Copy desktop texture to staging ---
    context->CopyResource(stagingTexture, desktopTexture);
    desktopTexture->Release();

    // --- Step 11: Map staging texture and read pixels ---
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);

    if (FAILED(hr)) {
        spdlog::debug("[ScreenCapture] Map staging texture failed");
        stagingTexture->Release();
        duplication->ReleaseFrame();
        duplication->Release();
        context->Release();
        device->Release();
        return result;
    }

    // Copy pixel data row by row (mapped.RowPitch may differ from width*4)
    const size_t rowBytes = static_cast<size_t>(outWidth) * 4;
    result.resize(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4);

    const auto* srcRow = static_cast<const uint8_t*>(mapped.pData);
    auto*       dstRow = result.data();

    for (int y = 0; y < outHeight; ++y) {
        std::memcpy(dstRow, srcRow, rowBytes);
        srcRow += mapped.RowPitch;
        dstRow += rowBytes;
    }

    // --- Cleanup ---
    context->Unmap(stagingTexture, 0);
    stagingTexture->Release();
    duplication->ReleaseFrame();
    duplication->Release();
    context->Release();
    device->Release();

    spdlog::debug("[ScreenCapture] DXGI capture {}x{} succeeded", outWidth, outHeight);
    return result;
}

// -----------------------------------------------------------------------
// GDI BitBlt capture
// -----------------------------------------------------------------------

/**
 * @brief Monitor info collected by the EnumDisplayMonitors callback.
 */
namespace {

struct MonitorInfo
{
    HMONITOR handle; ///< Monitor handle
    RECT     rect;   ///< Monitor rectangle in virtual screen coordinates
};

/**
 * @brief EnumDisplayMonitors callback that collects monitor info.
 *
 * @param hMonitor Monitor handle.
 * @param hdcMonitor Device context (unused).
 * @param lprcMonitor Monitor rectangle.
 * @param dwData User data pointer to std::vector<MonitorInfo>.
 * @return TRUE to continue enumeration.
 */
BOOL CALLBACK monitorEnumProc(HMONITOR hMonitor, HDC /*hdcMonitor*/,
                               LPRECT lprcMonitor, LPARAM dwData)
{
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);
    monitors->push_back({hMonitor, *lprcMonitor});
    return TRUE;
}

} // anonymous namespace

/**
 * @brief Captures the screen via GDI BitBlt.
 *
 * Steps:
 *   1. Enumerate monitors to find the requested one.
 *   2. Create a memory DC and compatible bitmap.
 *   3. BitBlt from the screen DC to the memory DC.
 *   4. GetDIBits to extract BGRA pixel data.
 *
 * If the requested monitor index is out of range, falls back to
 * the primary monitor (full virtual screen from GetDC(nullptr)).
 *
 * @param monitor   Monitor index.
 * @param outWidth  Receives image width.
 * @param outHeight Receives image height.
 * @return BGRA pixel data (top-to-bottom).
 */
std::vector<uint8_t> ScreenCapture::captureGdi(int monitor,
                                                 int& outWidth, int& outHeight)
{
    // Enumerate monitors
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc,
                        reinterpret_cast<LPARAM>(&monitors));

    int srcX = 0;
    int srcY = 0;
    int w    = 0;
    int h    = 0;

    if (monitor >= 0 && monitor < static_cast<int>(monitors.size())) {
        // Use the specific monitor's rectangle
        const auto& rect = monitors[static_cast<size_t>(monitor)].rect;
        srcX = rect.left;
        srcY = rect.top;
        w    = rect.right  - rect.left;
        h    = rect.bottom - rect.top;
    } else {
        // Fallback: primary monitor (full screen from GetDC(nullptr))
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }

    if (w <= 0 || h <= 0) {
        spdlog::warn("[ScreenCapture] GDI: invalid screen dimensions {}x{}", w, h);
        return {};
    }

    outWidth  = w;
    outHeight = h;

    // Get screen DC
    HDC screenDC = GetDC(nullptr);
    if (!screenDC) {
        spdlog::warn("[ScreenCapture] GDI: GetDC(nullptr) failed");
        return {};
    }

    // Create memory DC and compatible bitmap
    HDC     memDC = CreateCompatibleDC(screenDC);
    HBITMAP bmp   = CreateCompatibleBitmap(screenDC, w, h);

    if (!memDC || !bmp) {
        spdlog::warn("[ScreenCapture] GDI: CreateCompatibleDC/Bitmap failed");
        if (bmp)   DeleteObject(bmp);
        if (memDC) DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return {};
    }

    HGDIOBJ oldBmp = SelectObject(memDC, bmp);

    // BitBlt from screen to memory DC
    if (!BitBlt(memDC, 0, 0, w, h, screenDC, srcX, srcY, SRCCOPY)) {
        spdlog::warn("[ScreenCapture] GDI: BitBlt failed");
        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return {};
    }

    // Extract pixel data via GetDIBits
    BITMAPINFOHEADER bi{};
    bi.biSize        = sizeof(BITMAPINFOHEADER);
    bi.biWidth       = w;
    bi.biHeight      = -h; // negative = top-down row order
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);

    int lines = GetDIBits(memDC, bmp, 0, static_cast<UINT>(h),
                           pixels.data(),
                           reinterpret_cast<BITMAPINFO*>(&bi),
                           DIB_RGB_COLORS);

    // Cleanup GDI objects
    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    if (lines == 0) {
        spdlog::warn("[ScreenCapture] GDI: GetDIBits returned 0 lines");
        return {};
    }

    spdlog::debug("[ScreenCapture] GDI capture {}x{} succeeded ({} lines)", w, h, lines);
    return pixels;
}

// -----------------------------------------------------------------------
// JPEG encoding (via stb_image_write)
// -----------------------------------------------------------------------

namespace {

/**
 * @brief stb_image_write callback that appends data to a vector.
 *
 * @param context Pointer to the output std::vector<uint8_t>.
 * @param data    Pointer to JPEG data chunk.
 * @param size    Size of the data chunk in bytes.
 */
void jpegWriteCallback(void* context, void* data, int size)
{
    auto* vec   = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

} // anonymous namespace

/**
 * @brief Encodes BGRA pixel data to JPEG format.
 *
 * Converts BGRA (as produced by both DXGI and GDI) to RGB, then
 * uses stb_image_write's JPEG encoder to produce compressed output.
 *
 * @param pixels  BGRA pixel data, top-to-bottom.
 * @param width   Image width.
 * @param height  Image height.
 * @param quality JPEG quality 1-100.
 * @return JPEG byte data.
 */
std::vector<uint8_t> ScreenCapture::encodeJpeg(const std::vector<uint8_t>& pixels,
                                                 int width, int height, int quality)
{
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);

    // Convert BGRA → RGB
    std::vector<uint8_t> rgb(pixelCount * 3);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgb[i * 3 + 0] = pixels[i * 4 + 2]; // R ← B channel
        rgb[i * 3 + 1] = pixels[i * 4 + 1]; // G ← G channel
        rgb[i * 3 + 2] = pixels[i * 4 + 0]; // B ← R channel
    }

    // Encode to JPEG via stb
    std::vector<uint8_t> jpegData;
    jpegData.reserve(pixelCount / 4); // rough estimate

    int ok = stbi_write_jpg_to_func(jpegWriteCallback, &jpegData,
                                     width, height, 3,
                                     rgb.data(), quality);

    if (!ok) {
        spdlog::warn("[ScreenCapture] stbi_write_jpg_to_func failed");
        return {};
    }

    spdlog::debug("[ScreenCapture] JPEG encoded: {}x{} q={} → {} bytes",
                  width, height, quality, jpegData.size());
    return jpegData;
}

// -----------------------------------------------------------------------
// Base64 encoding
// -----------------------------------------------------------------------

/**
 * @brief Encodes binary data to base64.
 *
 * Uses the standard base64 alphabet (RFC 4648) with '=' padding.
 *
 * @param data Binary input data.
 * @return Base64-encoded string.
 */
std::string ScreenCapture::base64Encode(const std::vector<uint8_t>& data)
{
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    const size_t inputLen = data.size();
    // Output length: 4 chars per 3 input bytes, rounded up
    const size_t outputLen = 4 * ((inputLen + 2) / 3);

    std::string encoded;
    encoded.reserve(outputLen);

    size_t i = 0;
    while (i + 2 < inputLen) {
        const uint32_t triple =
            (static_cast<uint32_t>(data[i])     << 16) |
            (static_cast<uint32_t>(data[i + 1]) <<  8) |
            (static_cast<uint32_t>(data[i + 2]));

        encoded.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        encoded.push_back(kAlphabet[(triple >>  6) & 0x3F]);
        encoded.push_back(kAlphabet[(triple      ) & 0x3F]);

        i += 3;
    }

    // Handle remaining bytes
    if (i < inputLen) {
        const uint32_t a = data[i];
        const uint32_t b = (i + 1 < inputLen) ? data[i + 1] : 0;

        const uint32_t partial = (a << 16) | (b << 8);

        encoded.push_back(kAlphabet[(partial >> 18) & 0x3F]);
        encoded.push_back(kAlphabet[(partial >> 12) & 0x3F]);

        if (i + 1 < inputLen) {
            encoded.push_back(kAlphabet[(partial >> 6) & 0x3F]);
        } else {
            encoded.push_back('=');
        }
        encoded.push_back('=');
    }

    return encoded;
}

} // namespace hub32agent::features
