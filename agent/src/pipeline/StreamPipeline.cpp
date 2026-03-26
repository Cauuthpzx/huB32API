/**
 * @file StreamPipeline.cpp
 * @brief GPU-first streaming pipeline with 3-thread design.
 *
 * 3-thread architecture:
 *   Thread 1 (capture): DXGI/GDI → BGRA → FrameQueue.push()
 *   Thread 2 (encode):  FrameQueue.pop() → NV12 → H.264 → sendH264()
 *   Thread 3 (quality): Monitors CPU/RTT/loss, adjusts quality
 *
 * Pipeline selection on start():
 *   - Path A (full GPU):  DXGI → D3D11 VideoProcessor → NVENC → RTP  (TODO)
 *   - Path B (mixed):     DXGI → CPU color conv → NVENC → RTP
 *   - Path C (CPU only):  DXGI/GDI → CPU color conv → x264 → RTP
 */

#if defined(HUB32_WITH_FFMPEG) && defined(HUB32_WITH_WEBRTC)

// Windows + DirectX headers (must come before project headers)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include "hub32agent/pipeline/StreamPipeline.hpp"
#include "hub32agent/webrtc/WebRtcProducer.hpp"
#include "FrameQueue.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace hub32agent::pipeline {

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

const char* to_string(PipelinePath path)
{
    switch (path) {
        case PipelinePath::kNone:     return "none";
        case PipelinePath::kFullGpu:  return "full-gpu";
        case PipelinePath::kMixedGpu: return "mixed-gpu";
        case PipelinePath::kCpuOnly:  return "cpu-only";
    }
    return "unknown";
}

int64_t StreamPipeline::nowMs()
{
    using Clock = std::chrono::steady_clock;
    auto now = Clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

// -----------------------------------------------------------------------
// Lightweight COM RAII (same pattern as ScreenCapture.cpp)
// -----------------------------------------------------------------------
namespace {

template<typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { if (m_ptr) m_ptr->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** addressOf() { return &m_ptr; }
    T* get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }
    void reset() { if (m_ptr) { m_ptr->Release(); m_ptr = nullptr; } }
private:
    T* m_ptr = nullptr;
};

struct ComScope
{
    HRESULT hr;
    ComScope() { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
    bool ok() const { return SUCCEEDED(hr); }
};

/// @brief Converts a Windows FILETIME to a uint64_t for arithmetic.
uint64_t filetimeToU64(const FILETIME& ft)
{
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
           static_cast<uint64_t>(ft.dwLowDateTime);
}

/// @brief Monitor info for GDI capture enumeration.
struct MonitorInfo
{
    HMONITOR handle;
    RECT     rect;
};

BOOL CALLBACK monitorEnumProc(HMONITOR hMonitor, HDC /*hdcMonitor*/,
                               LPRECT lprcMonitor, LPARAM dwData)
{
    auto* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);
    monitors->push_back({hMonitor, *lprcMonitor});
    return TRUE;
}

} // anonymous namespace

// -----------------------------------------------------------------------
// Construction / Destruction
// -----------------------------------------------------------------------

StreamPipeline::StreamPipeline(webrtc::WebRtcProducer& producer)
    : m_producer(producer)
    , m_frameQueue(std::make_unique<RawFrameQueue>())
{
}

StreamPipeline::~StreamPipeline()
{
    stop();
}

// -----------------------------------------------------------------------
// start() — probe pipeline path and launch 3 threads
// -----------------------------------------------------------------------

bool StreamPipeline::start(const PipelineConfig& config)
{
    if (m_running.load(std::memory_order_acquire)) {
        spdlog::warn("[StreamPipeline] already running");
        return false;
    }

    m_config = config;
    m_stopFlag.store(false);

    // Probe the best pipeline path
    auto path = probePipeline(config);
    if (path == PipelinePath::kNone) {
        spdlog::error("[StreamPipeline] no viable pipeline path found");
        return false;
    }

    m_activePath.store(path, std::memory_order_release);
    spdlog::info("[StreamPipeline] selected path: {} ({}x{} @{} fps, {} kbps)",
                 to_string(path), config.width, config.height,
                 config.fps, config.bitrateKbps);

    // Notify state callback
    {
        std::lock_guard lock(m_cbMutex);
        if (m_stateCb) {
            m_stateCb(path, "pipeline started");
        }
    }

    // Initialize adaptive quality state
    m_qualityState = AdaptiveQualityState{};
    m_lastCpuCheckMs = nowMs();
    m_cpuInitialized = false;

    // Reset frame queue for reuse
    m_frameQueue->reset();

    // Launch 3 threads
    m_running.store(true, std::memory_order_release);
    m_captureThread = std::thread(&StreamPipeline::captureLoop, this);
    m_encodeThread  = std::thread(&StreamPipeline::encodeLoop,  this);
    m_qualityThread = std::thread(&StreamPipeline::qualityLoop,  this);

    return true;
}

// -----------------------------------------------------------------------
// stop() — graceful shutdown
// -----------------------------------------------------------------------

void StreamPipeline::stop()
{
    if (!m_running.load(std::memory_order_acquire) &&
        !m_captureThread.joinable()) {
        return;
    }

    spdlog::info("[StreamPipeline] stopping...");
    m_stopFlag.store(true);
    m_running.store(false, std::memory_order_release);

    // Unblock encode thread waiting on empty queue
    m_frameQueue->stop();

    if (m_captureThread.joinable()) m_captureThread.join();
    if (m_encodeThread.joinable())  m_encodeThread.join();
    if (m_qualityThread.joinable()) m_qualityThread.join();

    // Release encoder
    if (m_encoder) {
        m_encoder->shutdown();
        m_encoder.reset();
    }

    // Release color converter
    if (m_converter) {
        m_converter->shutdown();
        m_converter.reset();
    }

    m_activePath.store(PipelinePath::kNone, std::memory_order_release);

    {
        std::lock_guard lock(m_cbMutex);
        if (m_stateCb) {
            m_stateCb(PipelinePath::kNone, "pipeline stopped");
        }
    }

    spdlog::info("[StreamPipeline] stopped");
}

// -----------------------------------------------------------------------
// setStateCallback / updateTransportStats
// -----------------------------------------------------------------------

void StreamPipeline::setStateCallback(StateCallback cb)
{
    std::lock_guard lock(m_cbMutex);
    m_stateCb = std::move(cb);
}

void StreamPipeline::updateTransportStats(int rttMs, double packetLoss)
{
    std::lock_guard lock(m_statsMutex);
    m_rttMs = rttMs;
    m_packetLoss = packetLoss;
}

// -----------------------------------------------------------------------
// probePipeline() — select best path
// -----------------------------------------------------------------------

PipelinePath StreamPipeline::probePipeline(const PipelineConfig& config)
{
    encode::EncoderConfig encConfig;
    encConfig.width       = config.width;
    encConfig.height      = config.height;
    encConfig.fps         = config.fps;
    encConfig.bitrateKbps = config.bitrateKbps;
    encConfig.profile     = "baseline";

    // Try NVENC first (with best available color converter: D3D11 or CPU)
    auto nvenc = encode::EncoderFactory::createEncoder("nvenc", encConfig);
    if (nvenc) {
        auto conv = encode::EncoderFactory::createBestConverter(
            config.width, config.height);
        if (conv) {
            bool gpuConverter = (conv->name().find("d3d11") != std::string::npos);
            m_converter = std::move(conv);
            m_encoder = std::move(nvenc);

            if (gpuConverter) {
                spdlog::info("[StreamPipeline] Path A: NVENC + {} (full GPU)",
                             m_converter->name());
                return PipelinePath::kFullGpu;
            }
            spdlog::info("[StreamPipeline] Path B: NVENC + {} (mixed GPU/CPU)",
                         m_converter->name());
            return PipelinePath::kMixedGpu;
        }

        spdlog::warn("[StreamPipeline] NVENC available but no color converter");
        nvenc->shutdown();
    }

    // Path C: x264 (or QSV) + best color converter
    auto encoder = encode::EncoderFactory::createBestEncoder(encConfig);
    if (encoder) {
        auto conv = encode::EncoderFactory::createBestConverter(
            config.width, config.height);
        if (conv) {
            m_converter = std::move(conv);
            m_encoder = std::move(encoder);
            spdlog::info("[StreamPipeline] Path C: {} + {} color converter",
                         m_encoder->name(), m_converter->name());
            return PipelinePath::kCpuOnly;
        }
        encoder->shutdown();
    }

    return PipelinePath::kNone;
}

// -----------------------------------------------------------------------
// captureLoop() — Thread 1: capture → push to FrameQueue
// -----------------------------------------------------------------------

void StreamPipeline::captureLoop()
{
    spdlog::info("[StreamPipeline] capture thread started ({}x{} @{} fps)",
                 m_config.width, m_config.height, m_config.fps);

    const auto frameIntervalUs = static_cast<int64_t>(1'000'000 / m_config.fps);
    int64_t ptsUs = 0;

    while (!m_stopFlag.load()) {
        auto frameStart = std::chrono::steady_clock::now();

        // Capture BGRA frame
        int capturedWidth  = 0;
        int capturedHeight = 0;
        auto bgra = captureFrame(m_config.monitor, capturedWidth, capturedHeight);

        if (bgra.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Push to frame queue (drops oldest if full — prevents capture stall)
        RawFrame frame;
        frame.bgra   = std::move(bgra);
        frame.width  = capturedWidth;
        frame.height = capturedHeight;
        frame.ptsUs  = ptsUs;
        m_frameQueue->push(std::move(frame));

        ptsUs += frameIntervalUs;

        // Frame pacing — use current fps (may have been adjusted by quality thread)
        auto frameEnd = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            frameEnd - frameStart).count();

        int currentFps = m_config.fpsSteps[m_qualityState.currentFpsIdx];
        int64_t targetIntervalUs = 1'000'000 / currentFps;

        if (elapsed < targetIntervalUs) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(targetIntervalUs - elapsed));
        }
    }

    m_frameQueue->stop();  // signal encode thread to exit
    spdlog::info("[StreamPipeline] capture thread exited");
}

// -----------------------------------------------------------------------
// encodeLoop() — Thread 2: pop from queue → NV12 → encode → send
// -----------------------------------------------------------------------

void StreamPipeline::encodeLoop()
{
    spdlog::info("[StreamPipeline] encode thread started");

    std::vector<uint8_t> nv12Buffer;
    if (m_converter) {
        nv12Buffer.resize(m_converter->nv12BufferSize());
    }

    RawFrame frame;
    while (m_frameQueue->pop(frame)) {
        if (m_stopFlag.load()) break;

        if (!m_converter || !m_encoder) {
            spdlog::error("[StreamPipeline] converter or encoder is null");
            break;
        }

        // Color convert BGRA → NV12
        const int bgraStride = frame.width * 4;
        if (!m_converter->convert(frame.bgra.data(), bgraStride, nv12Buffer.data())) {
            spdlog::warn("[StreamPipeline] color conversion failed, skipping frame");
            continue;
        }

        // Encode NV12 → H.264 → RTP
        m_encoder->encode(
            nv12Buffer.data(), m_converter->nv12BufferSize(),
            frame.ptsUs,
            [this](const encode::EncodedPacket& packet) {
                if (m_producer.isConnected()) {
                    m_producer.sendH264(
                        packet.data.data(),
                        packet.data.size(),
                        packet.timestampUs,
                        packet.isKeyFrame
                    );
                }
            }
        );
    }

    spdlog::info("[StreamPipeline] encode thread exited");
}

// -----------------------------------------------------------------------
// qualityLoop() — Thread 3: monitors CPU/RTT/loss, adjusts quality
// -----------------------------------------------------------------------

void StreamPipeline::qualityLoop()
{
    spdlog::info("[StreamPipeline] quality thread started (interval={}ms)",
                  m_config.cpuCheckIntervalMs);

    while (!m_stopFlag.load()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(m_config.cpuCheckIntervalMs));
        if (m_stopFlag.load()) break;

        checkAdaptiveQuality();
    }

    spdlog::info("[StreamPipeline] quality thread exited");
}

// -----------------------------------------------------------------------
// captureFrame() — DXGI with GDI fallback
// -----------------------------------------------------------------------

std::vector<uint8_t> StreamPipeline::captureFrame(int monitor,
                                                    int& outWidth, int& outHeight)
{
    if (m_dxgiAvailable) {
        auto pixels = captureDxgi(monitor, outWidth, outHeight);
        if (!pixels.empty()) {
            return pixels;
        }
        m_dxgiAvailable = false;
        spdlog::info("[StreamPipeline] DXGI unavailable, falling back to GDI");
    }

    return captureGdi(monitor, outWidth, outHeight);
}

// -----------------------------------------------------------------------
// captureDxgi() — DXGI Desktop Duplication
// -----------------------------------------------------------------------

std::vector<uint8_t> StreamPipeline::captureDxgi(int monitor,
                                                   int& outWidth, int& outHeight)
{
    std::vector<uint8_t> result;

    ComScope com;
    if (!com.ok()) return result;

    // Create D3D11 device
    ComPtr<ID3D11Device>       device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        device.addressOf(), &featureLevel, context.addressOf()
    );
    if (FAILED(hr) || !device) return result;

    // Get DXGI device and adapter
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device->QueryInterface(IID_IDXGIDevice,
                                 reinterpret_cast<void**>(dxgiDevice.addressOf()));
    if (FAILED(hr) || !dxgiDevice) return result;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.addressOf());
    if (FAILED(hr) || !adapter) return result;

    // Enumerate outputs
    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(static_cast<UINT>(monitor), output.addressOf());
    if (FAILED(hr) || !output) return result;

    ComPtr<IDXGIOutput1> output1;
    hr = output->QueryInterface(IID_IDXGIOutput1,
                                 reinterpret_cast<void**>(output1.addressOf()));
    if (FAILED(hr) || !output1) return result;

    // Duplicate output
    ComPtr<IDXGIOutputDuplication> duplication;
    hr = output1->DuplicateOutput(device.get(), duplication.addressOf());
    if (FAILED(hr) || !duplication) return result;

    // Acquire frame
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;
    hr = duplication->AcquireNextFrame(100, &frameInfo, desktopResource.addressOf());
    if (FAILED(hr) || !desktopResource) return result;

    // Get texture
    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource->QueryInterface(IID_ID3D11Texture2D,
                                          reinterpret_cast<void**>(desktopTexture.addressOf()));
    if (FAILED(hr) || !desktopTexture) {
        duplication->ReleaseFrame();
        return result;
    }

    D3D11_TEXTURE2D_DESC texDesc{};
    desktopTexture->GetDesc(&texDesc);
    outWidth  = static_cast<int>(texDesc.Width);
    outHeight = static_cast<int>(texDesc.Height);

    // Create staging texture
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

    ComPtr<ID3D11Texture2D> stagingTexture;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.addressOf());
    if (FAILED(hr) || !stagingTexture) {
        duplication->ReleaseFrame();
        return result;
    }

    context->CopyResource(stagingTexture.get(), desktopTexture.get());

    // Map and read pixels
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        duplication->ReleaseFrame();
        return result;
    }

    const size_t rowBytes = static_cast<size_t>(outWidth) * 4;
    result.resize(static_cast<size_t>(outWidth) * outHeight * 4);

    const auto* srcRow = static_cast<const uint8_t*>(mapped.pData);
    auto* dstRow = result.data();
    for (int y = 0; y < outHeight; ++y) {
        std::memcpy(dstRow, srcRow, rowBytes);
        srcRow += mapped.RowPitch;
        dstRow += rowBytes;
    }

    context->Unmap(stagingTexture.get(), 0);
    duplication->ReleaseFrame();

    return result;
}

// -----------------------------------------------------------------------
// captureGdi() — GDI BitBlt fallback
// -----------------------------------------------------------------------

std::vector<uint8_t> StreamPipeline::captureGdi(int monitor,
                                                  int& outWidth, int& outHeight)
{
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc,
                        reinterpret_cast<LPARAM>(&monitors));

    int srcX = 0;
    int srcY = 0;
    int w = 0;
    int h = 0;

    if (monitor >= 0 && monitor < static_cast<int>(monitors.size())) {
        const auto& rect = monitors[static_cast<size_t>(monitor)].rect;
        srcX = rect.left;
        srcY = rect.top;
        w = rect.right - rect.left;
        h = rect.bottom - rect.top;
    } else {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }

    if (w <= 0 || h <= 0) return {};

    outWidth = w;
    outHeight = h;

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) return {};

    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bmp = CreateCompatibleBitmap(screenDC, w, h);

    if (!memDC || !bmp) {
        if (bmp) DeleteObject(bmp);
        if (memDC) DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return {};
    }

    HGDIOBJ oldBmp = SelectObject(memDC, bmp);

    if (!BitBlt(memDC, 0, 0, w, h, screenDC, srcX, srcY, SRCCOPY)) {
        SelectObject(memDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return {};
    }

    BITMAPINFOHEADER bi{};
    bi.biSize        = sizeof(BITMAPINFOHEADER);
    bi.biWidth       = w;
    bi.biHeight      = -h;
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    int lines = GetDIBits(memDC, bmp, 0, static_cast<UINT>(h),
                           pixels.data(),
                           reinterpret_cast<BITMAPINFO*>(&bi),
                           DIB_RGB_COLORS);

    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    if (lines == 0) return {};
    return pixels;
}

// -----------------------------------------------------------------------
// checkAdaptiveQuality() — CPU usage + transport stats
// -----------------------------------------------------------------------

void StreamPipeline::checkAdaptiveQuality()
{
    auto now = nowMs();
    m_lastCpuCheckMs = now;

    // --- CPU-based adaptation ---
    int cpuPercent = queryCpuUsage();

    if (cpuPercent >= m_config.cpuHighPercent) {
        if (m_qualityState.cpuHighSinceMs == 0) {
            m_qualityState.cpuHighSinceMs = now;
        }
        m_qualityState.cpuLowSinceMs = 0;

        int64_t highDuration = now - m_qualityState.cpuHighSinceMs;
        if (highDuration >= m_config.cpuThresholdSec * 1000) {
            // Reduce quality: try reducing fps first, then resolution
            bool adjusted = false;

            if (m_qualityState.currentFpsIdx < 2) {
                ++m_qualityState.currentFpsIdx;
                adjusted = true;
                spdlog::info("[StreamPipeline] CPU {}% > {}% for {}s — reducing fps to {}",
                             cpuPercent, m_config.cpuHighPercent,
                             m_config.cpuThresholdSec,
                             m_config.fpsSteps[m_qualityState.currentFpsIdx]);
            } else if (m_qualityState.currentResolutionIdx < 2) {
                ++m_qualityState.currentResolutionIdx;
                adjusted = true;
                spdlog::info("[StreamPipeline] CPU {}% > {}% for {}s — reducing resolution to {}",
                             cpuPercent, m_config.cpuHighPercent,
                             m_config.cpuThresholdSec,
                             m_config.resolutionSteps[m_qualityState.currentResolutionIdx]);
            }

            if (adjusted) {
                m_qualityState.cpuHighSinceMs = 0;
                if (m_encoder) {
                    int newBitrate = m_config.bitrateSteps[m_qualityState.currentBitrateIdx];
                    m_encoder->setBitrateAndKeyFrame(newBitrate);
                    m_producer.setPacingBitrate(newBitrate);
                }
            }
        }
    } else if (cpuPercent <= m_config.cpuLowPercent) {
        if (m_qualityState.cpuLowSinceMs == 0) {
            m_qualityState.cpuLowSinceMs = now;
        }
        m_qualityState.cpuHighSinceMs = 0;

        int64_t lowDuration = now - m_qualityState.cpuLowSinceMs;
        if (lowDuration >= m_config.cpuThresholdSec * 1000) {
            // Restore quality: try restoring resolution first, then fps
            bool restored = false;

            if (m_qualityState.currentResolutionIdx > 0) {
                --m_qualityState.currentResolutionIdx;
                restored = true;
                spdlog::info("[StreamPipeline] CPU {}% < {}% for {}s — restoring resolution to {}",
                             cpuPercent, m_config.cpuLowPercent,
                             m_config.cpuThresholdSec,
                             m_config.resolutionSteps[m_qualityState.currentResolutionIdx]);
            } else if (m_qualityState.currentFpsIdx > 0) {
                --m_qualityState.currentFpsIdx;
                restored = true;
                spdlog::info("[StreamPipeline] CPU {}% < {}% for {}s — restoring fps to {}",
                             cpuPercent, m_config.cpuLowPercent,
                             m_config.cpuThresholdSec,
                             m_config.fpsSteps[m_qualityState.currentFpsIdx]);
            }

            if (restored) {
                m_qualityState.cpuLowSinceMs = 0;
                if (m_encoder) {
                    int newBitrate = m_config.bitrateSteps[m_qualityState.currentBitrateIdx];
                    m_encoder->setBitrateAndKeyFrame(newBitrate);
                    m_producer.setPacingBitrate(newBitrate);
                }
            }
        }
    } else {
        // CPU is in normal range — reset both timers
        m_qualityState.cpuHighSinceMs = 0;
        m_qualityState.cpuLowSinceMs  = 0;
    }

    // --- Transport-based adaptation ---
    int rttMs = 0;
    double packetLoss = 0.0;
    {
        std::lock_guard lock(m_statsMutex);
        rttMs = m_rttMs;
        packetLoss = m_packetLoss;
    }

    // Packet loss > threshold: reduce bitrate
    if (packetLoss > m_config.packetLossHighPercent) {
        if (m_qualityState.currentBitrateIdx < 2) {
            ++m_qualityState.currentBitrateIdx;
            int newBitrate = m_config.bitrateSteps[m_qualityState.currentBitrateIdx];
            if (m_encoder) {
                m_encoder->setBitrateAndKeyFrame(newBitrate);
                m_producer.setPacingBitrate(newBitrate);
            }
            spdlog::info("[StreamPipeline] packet loss {:.1f}% > {:.1f}% — reducing bitrate to {} kbps",
                         packetLoss, m_config.packetLossHighPercent, newBitrate);
        }
    } else if (packetLoss < m_config.packetLossHighPercent / 2.0) {
        // Packet loss well below threshold: try restoring bitrate
        if (m_qualityState.currentBitrateIdx > 0) {
            --m_qualityState.currentBitrateIdx;
            int newBitrate = m_config.bitrateSteps[m_qualityState.currentBitrateIdx];
            if (m_encoder) {
                m_encoder->setBitrateAndKeyFrame(newBitrate);
                m_producer.setPacingBitrate(newBitrate);
            }
            spdlog::info("[StreamPipeline] packet loss {:.1f}% recovered — restoring bitrate to {} kbps",
                         packetLoss, newBitrate);
        }
    }

    // High RTT: reduce fps
    if (rttMs > m_config.rttHighMs) {
        if (m_qualityState.currentFpsIdx < 2) {
            ++m_qualityState.currentFpsIdx;
            spdlog::info("[StreamPipeline] RTT {}ms > {}ms — reducing fps to {}",
                         rttMs, m_config.rttHighMs,
                         m_config.fpsSteps[m_qualityState.currentFpsIdx]);
        }
    }
}

// -----------------------------------------------------------------------
// queryCpuUsage() — Windows GetSystemTimes
// -----------------------------------------------------------------------

int StreamPipeline::queryCpuUsage()
{
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};

    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return 0;
    }

    uint64_t idle   = filetimeToU64(idleTime);
    uint64_t kernel = filetimeToU64(kernelTime);
    uint64_t user   = filetimeToU64(userTime);

    if (!m_cpuInitialized) {
        m_prevIdleTime   = idle;
        m_prevKernelTime = kernel;
        m_prevUserTime   = user;
        m_cpuInitialized = true;
        return 0;
    }

    uint64_t deltaIdle   = idle   - m_prevIdleTime;
    uint64_t deltaKernel = kernel - m_prevKernelTime;
    uint64_t deltaUser   = user   - m_prevUserTime;

    m_prevIdleTime   = idle;
    m_prevKernelTime = kernel;
    m_prevUserTime   = user;

    uint64_t totalSystem = deltaKernel + deltaUser;
    if (totalSystem == 0) return 0;

    // kernel time includes idle time
    uint64_t totalBusy = totalSystem - deltaIdle;
    return static_cast<int>(totalBusy * 100 / totalSystem);
}

} // namespace hub32agent::pipeline

#endif // HUB32_WITH_FFMPEG && HUB32_WITH_WEBRTC
