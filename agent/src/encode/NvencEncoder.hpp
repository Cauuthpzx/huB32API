#pragma once

#ifdef HUB32_WITH_FFMPEG

#include "hub32agent/encode/H264Encoder.hpp"
#include <memory>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace hub32agent::encode {

/// @brief H.264 encoder using FFmpeg with NVIDIA NVENC (GPU hardware encoding).
/// Requires NVIDIA GPU with NVENC support and appropriate driver.
/// Falls back gracefully if GPU/driver not available.
class NvencEncoder : public H264Encoder
{
public:
    NvencEncoder();
    ~NvencEncoder() override;

    std::string name() const override { return "nvenc"; }
    bool initialize(const EncoderConfig& config) override;
    void encode(const uint8_t* nv12Data, size_t dataSize,
                int64_t timestampUs,
                std::function<void(const EncodedPacket&)> callback) override;
    void requestKeyFrame() override;
    void setBitrate(int kbps) override;
    void shutdown() override;

private:
    AVCodecContext* ctx_    = nullptr;
    AVFrame*        frame_  = nullptr;
    AVPacket*       pkt_    = nullptr;
    EncoderConfig   config_;
    bool            force_key_frame_ = false;
    int64_t         frame_count_     = 0;
};

} // namespace hub32agent::encode

#endif // HUB32_WITH_FFMPEG
