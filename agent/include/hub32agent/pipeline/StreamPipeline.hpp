/**
 * @file StreamPipeline.hpp
 * @brief GPU-first streaming pipeline with automatic fallback chain.
 *
 * Pipeline paths (selected at start(), best to worst):
 *
 *   Path A — Full GPU, zero-copy:
 *     DXGI → D3D11 VideoProcessor BGRA→NV12 → NVENC → WebRTC RTP
 *
 *   Path B — Mixed GPU/CPU:
 *     DXGI → CPU copy BGRA → CpuColorConverter NV12 → NVENC → WebRTC RTP
 *
 *   Path C — CPU fallback (always works):
 *     DXGI/GDI → BGRA CPU → CpuColorConverter NV12 → x264 → WebRTC RTP
 *
 * 3-thread design:
 *   Thread 1 (capture): DXGI/GDI → BGRA → FrameQueue.push()
 *   Thread 2 (encode):  FrameQueue.pop() → NV12 → H.264 → sendH264()
 *   Thread 3 (quality): Monitors CPU/RTT/packet loss, adjusts quality
 *
 * Includes adaptive quality control based on CPU usage, RTT, and packet loss.
 */

#pragma once

#if defined(HUB32_WITH_FFMPEG) && defined(HUB32_WITH_WEBRTC)

#include "hub32agent/encode/H264Encoder.hpp"
#include "hub32agent/encode/ColorConverter.hpp"
#include "hub32agent/encode/EncoderFactory.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Forward-declare FrameQueue types (defined in src/pipeline/FrameQueue.hpp)
namespace hub32agent::pipeline {
struct RawFrame;
template<typename T, size_t Capacity> class BoundedQueue;
using RawFrameQueue = BoundedQueue<RawFrame, 4>;
} // namespace hub32agent::pipeline

namespace hub32agent::webrtc {
class WebRtcProducer;
} // namespace hub32agent::webrtc

namespace hub32agent::pipeline {

// -----------------------------------------------------------------------
// Pipeline path enum
// -----------------------------------------------------------------------
enum class PipelinePath
{
    kNone,       ///< Not started
    kFullGpu,    ///< Path A: DXGI → D3D11 color conv → NVENC
    kMixedGpu,   ///< Path B: DXGI → CPU color conv → NVENC
    kCpuOnly     ///< Path C: DXGI/GDI → CPU color conv → x264
};

/// @brief Returns a human-readable name for the pipeline path.
const char* to_string(PipelinePath path);

// -----------------------------------------------------------------------
// Pipeline configuration
// -----------------------------------------------------------------------
struct PipelineConfig
{
    int width              = 1920;   // pixels
    int height             = 1080;   // pixels
    int fps                = 30;     // frames/second
    int bitrateKbps        = 2500;   // kbps
    int monitor            = 0;      // monitor index (0 = primary)

    // Adaptive quality thresholds
    int cpuHighPercent     = 80;     // percent — reduce quality above this
    int cpuLowPercent      = 50;     // percent — restore quality below this
    int cpuCheckIntervalMs = 5000;   // milliseconds — CPU usage check interval
    int cpuThresholdSec    = 10;     // seconds — sustained CPU before adjustment

    double packetLossHighPercent = 5.0;   // percent — reduce bitrate above this
    int rttHighMs          = 200;    // milliseconds — reduce fps above this

    // Resolution steps for downscaling (widths)
    // Height computed from aspect ratio
    int resolutionSteps[3] = {1920, 1280, 960};
    int fpsSteps[3]        = {30, 15, 10};
    int bitrateSteps[3]    = {2500, 1000, 500}; // kbps
};

// -----------------------------------------------------------------------
// Adaptive quality state
// -----------------------------------------------------------------------
struct AdaptiveQualityState
{
    int currentResolutionIdx = 0;  // index into resolutionSteps
    int currentFpsIdx        = 0;  // index into fpsSteps
    int currentBitrateIdx    = 0;  // index into bitrateSteps

    int64_t cpuHighSinceMs   = 0;  // timestamp when CPU first went high
    int64_t cpuLowSinceMs    = 0;  // timestamp when CPU first went low
};

// -----------------------------------------------------------------------
// StreamPipeline
// -----------------------------------------------------------------------

/// @brief GPU-first streaming pipeline with automatic fallback.
///
/// Thread safety: start()/stop() are NOT re-entrant. The pipeline runs
/// three internal threads; all public methods are safe to call from any thread.
class StreamPipeline
{
public:
    /// @brief Callback for pipeline state changes.
    using StateCallback = std::function<void(PipelinePath path, const std::string& detail)>;

    explicit StreamPipeline(webrtc::WebRtcProducer& producer);
    ~StreamPipeline();

    // Non-copyable, non-movable
    StreamPipeline(const StreamPipeline&) = delete;
    StreamPipeline& operator=(const StreamPipeline&) = delete;
    StreamPipeline(StreamPipeline&&) = delete;
    StreamPipeline& operator=(StreamPipeline&&) = delete;

    /// @brief Starts the pipeline. Probes GPU capabilities and selects the
    ///        best path (A → B → C). Launches 3 threads: capture, encode, quality.
    /// @param config Pipeline configuration.
    /// @return true if pipeline started successfully.
    bool start(const PipelineConfig& config);

    /// @brief Stops the pipeline gracefully. Waits for all threads
    ///        to finish, flushes the encoder, and releases all resources.
    void stop();

    /// @brief Returns true if the pipeline is running.
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

    /// @brief Returns the currently active pipeline path.
    PipelinePath activePath() const { return m_activePath.load(std::memory_order_acquire); }

    /// @brief Sets a callback for pipeline state changes.
    void setStateCallback(StateCallback cb);

    /// @brief Updates adaptive quality based on external transport stats.
    /// @param rttMs      Current round-trip time in milliseconds.
    /// @param packetLoss Current packet loss percentage (0.0-100.0).
    void updateTransportStats(int rttMs, double packetLoss);

private:
    // ---- 3-thread pipeline ----
    void captureLoop();    ///< Thread 1: DXGI/GDI capture → pushes RawFrame to queue
    void encodeLoop();     ///< Thread 2: pops RawFrame → NV12 → H.264 → sendH264
    void qualityLoop();    ///< Thread 3: monitors CPU/RTT/loss, adjusts quality

    /// @brief Probes and selects the best pipeline path.
    PipelinePath probePipeline(const PipelineConfig& config);

    /// @brief Captures a single BGRA frame from screen.
    std::vector<uint8_t> captureFrame(int monitor, int& outWidth, int& outHeight);

    /// @brief DXGI Desktop Duplication capture.
    std::vector<uint8_t> captureDxgi(int monitor, int& outWidth, int& outHeight);

    /// @brief GDI BitBlt capture (fallback).
    std::vector<uint8_t> captureGdi(int monitor, int& outWidth, int& outHeight);

    /// @brief Runs adaptive quality checks (CPU usage, transport stats).
    void checkAdaptiveQuality();

    /// @brief Queries current system CPU usage percentage.
    int queryCpuUsage();

    /// @brief Gets monotonic clock in milliseconds.
    static int64_t nowMs();

    // ---- Members ----
    webrtc::WebRtcProducer& m_producer;

    std::atomic<bool>         m_running{false};
    std::atomic<bool>         m_stopFlag{false};
    std::atomic<PipelinePath> m_activePath{PipelinePath::kNone};

    // 3-thread handles
    std::thread m_captureThread;
    std::thread m_encodeThread;
    std::thread m_qualityThread;

    // Frame queue between capture and encode threads
    std::unique_ptr<RawFrameQueue> m_frameQueue;

    // Encoder + color converter (owned by pipeline)
    std::unique_ptr<encode::H264Encoder>   m_encoder;
    std::unique_ptr<encode::ColorConverter> m_converter;

    // Pipeline config (immutable after start)
    PipelineConfig m_config;

    // Adaptive quality state (accessed only from quality thread)
    AdaptiveQualityState m_qualityState;

    // Transport stats (written from external thread, read from quality thread)
    mutable std::mutex m_statsMutex;
    int    m_rttMs       = 0;
    double m_packetLoss  = 0.0;

    // DXGI state: whether DXGI is available for capture
    bool m_dxgiAvailable = true;

    // State callback
    mutable std::mutex m_cbMutex;
    StateCallback      m_stateCb;

    // CPU usage measurement (Windows FILETIME-based)
    int64_t m_lastCpuCheckMs   = 0;
    uint64_t m_prevIdleTime    = 0;
    uint64_t m_prevKernelTime  = 0;
    uint64_t m_prevUserTime    = 0;
    bool     m_cpuInitialized  = false;
};

} // namespace hub32agent::pipeline

#endif // HUB32_WITH_FFMPEG && HUB32_WITH_WEBRTC
