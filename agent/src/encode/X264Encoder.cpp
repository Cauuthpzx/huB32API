#ifdef HUB32_WITH_FFMPEG

#include "X264Encoder.hpp"
#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

namespace hub32agent::encode {

X264Encoder::X264Encoder() = default;

X264Encoder::~X264Encoder()
{
    shutdown();
}

bool X264Encoder::initialize(const EncoderConfig& config)
{
    config_ = config;

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        spdlog::error("[X264Encoder] libx264 codec not found in FFmpeg");
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) {
        spdlog::error("[X264Encoder] failed to allocate codec context");
        return false;
    }

    // Resolution — pixels
    ctx_->width  = config.width;
    ctx_->height = config.height;

    // Pixel format: NV12 input
    ctx_->pix_fmt = AV_PIX_FMT_NV12;

    // Timing — frames per second
    ctx_->time_base = {1, config.fps};
    ctx_->framerate = {config.fps, 1};

    // Bitrate — bits per second (config is in kbps)
    ctx_->bit_rate = static_cast<int64_t>(config.bitrateKbps) * 1000;

    // GOP — keyframe every N frames
    ctx_->gop_size = config.fps * config.keyFrameIntervalSec;

    // No B-frames for real-time streaming
    ctx_->max_b_frames = 0;

    // Threading — 2 threads for low-latency encoding
    ctx_->thread_count = 2;

    // x264-specific: ultrafast preset, zerolatency tune
    av_opt_set(ctx_->priv_data, "preset",  "ultrafast",   0);
    av_opt_set(ctx_->priv_data, "tune",    "zerolatency", 0);

    // H.264 profile
    if (config.profile == "baseline") {
        ctx_->profile = AV_PROFILE_H264_BASELINE;
    } else if (config.profile == "main") {
        ctx_->profile = AV_PROFILE_H264_MAIN;
    } else if (config.profile == "high") {
        ctx_->profile = AV_PROFILE_H264_HIGH;
    }

    // Open codec
    int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        spdlog::error("[X264Encoder] avcodec_open2 failed: {}", err);
        avcodec_free_context(&ctx_);
        return false;
    }

    // Allocate frame
    frame_ = av_frame_alloc();
    if (!frame_) {
        spdlog::error("[X264Encoder] failed to allocate frame");
        shutdown();
        return false;
    }
    frame_->format = AV_PIX_FMT_NV12;
    frame_->width  = config.width;
    frame_->height = config.height;

    // Allocate packet
    pkt_ = av_packet_alloc();
    if (!pkt_) {
        spdlog::error("[X264Encoder] failed to allocate packet");
        shutdown();
        return false;
    }

    frame_count_ = 0;
    spdlog::info("[X264Encoder] initialized: {}x{} @{} fps, {} kbps, preset=ultrafast, tune=zerolatency",
                 config.width, config.height, config.fps, config.bitrateKbps);
    return true;
}

void X264Encoder::encode(const uint8_t* nv12Data, size_t dataSize,
                          int64_t timestampUs,
                          std::function<void(const EncodedPacket&)> callback)
{
    if (!ctx_ || !frame_ || !pkt_) return;

    // Fill frame planes from NV12 data
    // NV12 layout: Y plane (width * height bytes) + UV plane (width * height / 2 bytes)
    const int ySize  = config_.width * config_.height;
    const int uvSize = config_.width * config_.height / 2;
    if (static_cast<int>(dataSize) < ySize + uvSize) return;

    frame_->data[0]     = const_cast<uint8_t*>(nv12Data);           // Y plane
    frame_->linesize[0] = config_.width;                            // bytes per row
    frame_->data[1]     = const_cast<uint8_t*>(nv12Data + ySize);   // UV interleaved
    frame_->linesize[1] = config_.width;                            // bytes per row

    frame_->pts = frame_count_++;

    // Force keyframe if requested
    if (force_key_frame_) {
        frame_->pict_type = AV_PICTURE_TYPE_I;
        force_key_frame_ = false;
    } else {
        frame_->pict_type = AV_PICTURE_TYPE_NONE;
    }

    // Send frame to encoder
    int ret = avcodec_send_frame(ctx_, frame_);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        spdlog::warn("[X264Encoder] avcodec_send_frame failed: {}", err);
        return;
    }

    // Receive encoded packets
    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            char err[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, err, sizeof(err));
            spdlog::warn("[X264Encoder] avcodec_receive_packet failed: {}", err);
            break;
        }

        EncodedPacket packet;
        packet.data.assign(pkt_->data, pkt_->data + pkt_->size);
        packet.timestampUs = timestampUs;
        packet.isKeyFrame  = (pkt_->flags & AV_PKT_FLAG_KEY) != 0;

        callback(packet);

        av_packet_unref(pkt_);
    }
}

void X264Encoder::requestKeyFrame()
{
    force_key_frame_ = true;
}

void X264Encoder::setBitrate(int kbps)
{
    if (!ctx_) return;
    ctx_->bit_rate = static_cast<int64_t>(kbps) * 1000; // kbps to bps
    config_.bitrateKbps = kbps;
    spdlog::info("[X264Encoder] bitrate changed to {} kbps", kbps);
}

void X264Encoder::shutdown()
{
    if (pkt_) {
        av_packet_free(&pkt_);
        pkt_ = nullptr;
    }
    if (frame_) {
        // Don't free frame data — we don't own it (it points to external NV12 buffer)
        frame_->data[0] = nullptr;
        frame_->data[1] = nullptr;
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (ctx_) {
        avcodec_free_context(&ctx_);
        ctx_ = nullptr;
    }
    spdlog::debug("[X264Encoder] shutdown");
}

} // namespace hub32agent::encode

#endif // HUB32_WITH_FFMPEG
