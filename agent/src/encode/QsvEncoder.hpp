#pragma once

#ifdef HUB32_WITH_FFMPEG

#include "hub32agent/encode/H264Encoder.hpp"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace hub32agent::encode {

/// @brief Intel Quick Sync Video (QSV) H.264 encoder via FFmpeg.
/// Uses h264_qsv codec for hardware-accelerated encoding on Intel GPUs.
/// Falls back gracefully if Intel GPU or QSV driver is not available.
class QsvEncoder : public H264Encoder
{
public:
    QsvEncoder() = default;
    ~QsvEncoder() override;

    std::string name() const override { return "qsv"; }
    bool initialize(const EncoderConfig& config) override;
    void encode(const uint8_t* nv12Data, size_t dataSize,
                int64_t timestampUs,
                std::function<void(const EncodedPacket&)> callback) override;
    void requestKeyFrame() override;
    void setBitrate(int kbps) override;
    void shutdown() override;

private:
    AVCodecContext* ctx_   = nullptr;
    AVFrame*        frame_ = nullptr;
    AVPacket*       pkt_   = nullptr;
    bool forceKeyFrame_    = false;
    int64_t frameCount_    = 0;
};

} // namespace hub32agent::encode

#endif // HUB32_WITH_FFMPEG
