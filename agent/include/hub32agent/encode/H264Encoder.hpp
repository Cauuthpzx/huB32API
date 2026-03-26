#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hub32agent::encode {

// -----------------------------------------------------------------------
// Encoder configuration
// -----------------------------------------------------------------------
struct EncoderConfig
{
    int width        = 1920;   // pixels — input frame width
    int height       = 1080;   // pixels — input frame height
    int fps          = 30;     // frames/second — target frame rate
    int bitrateKbps  = 2500;   // kbps — target bitrate
    int keyFrameIntervalSec = 2; // seconds — max interval between IDR frames
    std::string profile = "baseline"; // H.264 profile (baseline, main, high)
};

// -----------------------------------------------------------------------
// Encoded packet
// -----------------------------------------------------------------------
struct EncodedPacket
{
    std::vector<uint8_t> data;        ///< H.264 NAL unit(s)
    int64_t              timestampUs; ///< presentation timestamp in microseconds
    bool                 isKeyFrame;  ///< true if this is an IDR frame
};

// -----------------------------------------------------------------------
// H264Encoder — abstract interface for hardware/software H.264 encoding
//
// Implementations:
//   NvencEncoder  — NVIDIA GPU (lowest latency, best quality)
//   QsvEncoder    — Intel iGPU (Quick Sync Video)
//   X264Encoder   — CPU fallback (always available)
// -----------------------------------------------------------------------
class H264Encoder
{
public:
    virtual ~H264Encoder() = default;

    /// @brief Returns the encoder name (e.g., "nvenc", "qsv", "x264").
    virtual std::string name() const = 0;

    /// @brief Initializes the encoder with the given configuration.
    /// @return true on success, false if this encoder is not available.
    virtual bool initialize(const EncoderConfig& config) = 0;

    /// @brief Encodes a single NV12 frame.
    /// @param nv12Data    Pointer to NV12 pixel data (Y plane + interleaved UV plane).
    /// @param dataSize    Size of nv12Data in bytes (width * height * 3 / 2).
    /// @param timestampUs Presentation timestamp in microseconds.
    /// @param callback    Called with encoded packet(s). May be called 0 or more times.
    virtual void encode(const uint8_t* nv12Data, size_t dataSize,
                        int64_t timestampUs,
                        std::function<void(const EncodedPacket&)> callback) = 0;

    /// @brief Requests the next encoded frame to be a keyframe (IDR).
    virtual void requestKeyFrame() = 0;

    /// @brief Dynamically changes the target bitrate.
    /// @param kbps New target bitrate in kilobits per second.
    virtual void setBitrate(int kbps) = 0;

    /// @brief Releases encoder resources.
    virtual void shutdown() = 0;
};

} // namespace hub32agent::encode
