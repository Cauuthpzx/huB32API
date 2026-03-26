#pragma once

#ifdef HUB32_WITH_FFMPEG

#include "hub32agent/encode/H264Encoder.hpp"
#include <memory>

// Forward declarations — avoid including heavy FFmpeg headers in the header
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace hub32agent::encode {

/// @brief H.264 encoder using FFmpeg libavcodec with libx264 (CPU).
/// Settings: preset=ultrafast, tune=zerolatency, no B-frames.
class X264Encoder : public H264Encoder
{
public:
    X264Encoder();
    ~X264Encoder() override;

    std::string name() const override { return "x264"; }
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
