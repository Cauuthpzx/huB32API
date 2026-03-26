#ifdef HUB32_WITH_FFMPEG

#include "QsvEncoder.hpp"
#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

namespace hub32agent::encode {

QsvEncoder::~QsvEncoder()
{
    shutdown();
}

bool QsvEncoder::initialize(const EncoderConfig& config)
{
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_qsv");
    if (!codec) {
        spdlog::debug("[QsvEncoder] h264_qsv codec not found in FFmpeg build");
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) {
        spdlog::error("[QsvEncoder] failed to allocate codec context");
        return false;
    }

    ctx_->width     = config.width;
    ctx_->height    = config.height;
    ctx_->time_base = {1, config.fps};
    ctx_->framerate = {config.fps, 1};
    ctx_->bit_rate  = static_cast<int64_t>(config.bitrateKbps) * 1000;
    ctx_->gop_size  = config.fps * config.keyFrameIntervalSec;
    ctx_->max_b_frames = 0;
    ctx_->pix_fmt   = AV_PIX_FMT_NV12;
    ctx_->thread_count = 1;

    // QSV-specific options
    av_opt_set(ctx_->priv_data, "preset", "veryfast", 0);
    av_opt_set(ctx_->priv_data, "look_ahead", "0", 0);
    av_opt_set(ctx_->priv_data, "async_depth", "1", 0);

    int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        spdlog::debug("[QsvEncoder] avcodec_open2 failed (Intel GPU may not support QSV): {}",
                      ret);
        avcodec_free_context(&ctx_);
        return false;
    }

    frame_ = av_frame_alloc();
    pkt_   = av_packet_alloc();
    if (!frame_ || !pkt_) {
        spdlog::error("[QsvEncoder] failed to allocate frame/packet");
        shutdown();
        return false;
    }

    frame_->format = ctx_->pix_fmt;
    frame_->width  = ctx_->width;
    frame_->height = ctx_->height;

    // Dry-run: verify QSV actually works on this hardware
    if (av_frame_get_buffer(frame_, 0) < 0) {
        spdlog::debug("[QsvEncoder] frame buffer allocation failed");
        shutdown();
        return false;
    }

    // Fill with gray and try encoding
    std::memset(frame_->data[0], 128,
                static_cast<size_t>(frame_->linesize[0]) * ctx_->height);
    std::memset(frame_->data[1], 128,
                static_cast<size_t>(frame_->linesize[1]) * (ctx_->height / 2));
    frame_->pts = 0;

    ret = avcodec_send_frame(ctx_, frame_);
    if (ret < 0) {
        spdlog::debug("[QsvEncoder] dry-run send_frame failed: {} — Intel QSV unavailable", ret);
        shutdown();
        return false;
    }

    ret = avcodec_receive_packet(ctx_, pkt_);
    av_packet_unref(pkt_);
    av_frame_unref(frame_);

    // Re-allocate frame for reuse
    frame_->format = ctx_->pix_fmt;
    frame_->width  = ctx_->width;
    frame_->height = ctx_->height;

    frameCount_ = 1; // dry-run used pts=0

    spdlog::info("[QsvEncoder] initialized: {}x{} @{} fps, {} kbps (Intel QSV H.264)",
                 config.width, config.height, config.fps, config.bitrateKbps);
    return true;
}

void QsvEncoder::encode(const uint8_t* nv12Data, size_t dataSize,
                         int64_t timestampUs,
                         std::function<void(const EncodedPacket&)> callback)
{
    if (!ctx_ || !frame_ || !pkt_ || !nv12Data) return;

    if (av_frame_get_buffer(frame_, 0) < 0) return;
    av_frame_make_writable(frame_);

    // Map NV12 data to frame planes
    const int ySize = ctx_->width * ctx_->height;
    for (int y = 0; y < ctx_->height; ++y) {
        std::memcpy(frame_->data[0] + y * frame_->linesize[0],
                    nv12Data + y * ctx_->width,
                    ctx_->width);
    }
    const uint8_t* uvSrc = nv12Data + ySize;
    for (int y = 0; y < ctx_->height / 2; ++y) {
        std::memcpy(frame_->data[1] + y * frame_->linesize[1],
                    uvSrc + y * ctx_->width,
                    ctx_->width);
    }

    frame_->pts = frameCount_++;

    if (forceKeyFrame_) {
        frame_->pict_type = AV_PICTURE_TYPE_I;
        frame_->flags |= AV_FRAME_FLAG_KEY;
        forceKeyFrame_ = false;
    } else {
        frame_->pict_type = AV_PICTURE_TYPE_NONE;
        frame_->flags &= ~AV_FRAME_FLAG_KEY;
    }

    int ret = avcodec_send_frame(ctx_, frame_);
    av_frame_unref(frame_);
    frame_->format = ctx_->pix_fmt;
    frame_->width  = ctx_->width;
    frame_->height = ctx_->height;

    if (ret < 0) {
        spdlog::warn("[QsvEncoder] send_frame error: {}", ret);
        return;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            spdlog::warn("[QsvEncoder] receive_packet error: {}", ret);
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

void QsvEncoder::requestKeyFrame()
{
    forceKeyFrame_ = true;
}

void QsvEncoder::setBitrate(int kbps)
{
    if (ctx_) {
        ctx_->bit_rate = static_cast<int64_t>(kbps) * 1000;
    }
}

void QsvEncoder::shutdown()
{
    if (ctx_) {
        // Flush
        avcodec_send_frame(ctx_, nullptr);
        if (pkt_) {
            while (avcodec_receive_packet(ctx_, pkt_) == 0) {
                av_packet_unref(pkt_);
            }
        }
    }
    if (pkt_)   { av_packet_free(&pkt_);           pkt_   = nullptr; }
    if (frame_) { av_frame_free(&frame_);           frame_ = nullptr; }
    if (ctx_)   { avcodec_free_context(&ctx_);      ctx_   = nullptr; }
    frameCount_ = 0;
    forceKeyFrame_ = false;
}

} // namespace hub32agent::encode

#endif // HUB32_WITH_FFMPEG
