#ifdef HUB32_WITH_FFMPEG

#include "NvencEncoder.hpp"
#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

namespace hub32agent::encode {

NvencEncoder::NvencEncoder() = default;

NvencEncoder::~NvencEncoder()
{
    shutdown();
}

bool NvencEncoder::initialize(const EncoderConfig& config)
{
    config_ = config;

    const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!codec) {
        spdlog::debug("[NvencEncoder] h264_nvenc codec not found in FFmpeg build");
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) {
        spdlog::debug("[NvencEncoder] failed to allocate codec context");
        return false;
    }

    // Resolution
    ctx_->width  = config.width;
    ctx_->height = config.height;

    // Pixel format: NV12 input (native for NVENC)
    ctx_->pix_fmt = AV_PIX_FMT_NV12;

    // Timing
    ctx_->time_base = {1, config.fps};
    ctx_->framerate = {config.fps, 1};

    // Bitrate (CBR)
    ctx_->bit_rate = static_cast<int64_t>(config.bitrateKbps) * 1000;
    ctx_->rc_max_rate = ctx_->bit_rate;
    ctx_->rc_buffer_size = static_cast<int>(ctx_->bit_rate);  // 1 second buffer

    // GOP: keyframe every N seconds
    ctx_->gop_size = config.fps * config.keyFrameIntervalSec;

    // No B-frames for real-time streaming
    ctx_->max_b_frames = 0;

    // Single thread — NVENC handles its own threading on the GPU
    ctx_->thread_count = 1;

    // NVENC-specific options
    av_opt_set(ctx_->priv_data, "preset",  "p1",  0);   // fastest preset
    av_opt_set(ctx_->priv_data, "tune",    "ll",  0);   // low latency
    av_opt_set(ctx_->priv_data, "rc",      "cbr", 0);   // constant bitrate
    av_opt_set(ctx_->priv_data, "delay",   "0",   0);   // zero frame delay
    av_opt_set(ctx_->priv_data, "zerolatency", "1", 0);
    av_opt_set_int(ctx_->priv_data, "b_adapt", 0, 0);   // disable B-frame adaptation

    // H.264 profile
    if (config.profile == "baseline") {
        av_opt_set(ctx_->priv_data, "profile", "baseline", 0);
    } else if (config.profile == "main") {
        av_opt_set(ctx_->priv_data, "profile", "main", 0);
    } else if (config.profile == "high") {
        av_opt_set(ctx_->priv_data, "profile", "high", 0);
    }

    // Try to open — this is where NVENC availability is actually tested.
    // If no NVIDIA GPU or driver doesn't support NVENC, this fails.
    int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        char err[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err, sizeof(err));
        spdlog::debug("[NvencEncoder] avcodec_open2 failed (GPU likely not available): {}", err);
        avcodec_free_context(&ctx_);
        return false;
    }

    // Allocate frame
    frame_ = av_frame_alloc();
    if (!frame_) {
        spdlog::error("[NvencEncoder] failed to allocate frame");
        shutdown();
        return false;
    }
    frame_->format = AV_PIX_FMT_NV12;
    frame_->width  = config.width;
    frame_->height = config.height;

    // Allocate packet
    pkt_ = av_packet_alloc();
    if (!pkt_) {
        spdlog::error("[NvencEncoder] failed to allocate packet");
        shutdown();
        return false;
    }

    // Dry-run: encode a single dummy frame to verify the GPU actually works
    // (avcodec_open2 can succeed even if the GPU is in a bad state)
    {
        const size_t nv12Size = static_cast<size_t>(config.width) * config.height * 3 / 2;
        std::vector<uint8_t> dummy(nv12Size, 128);

        frame_->data[0]     = dummy.data();
        frame_->linesize[0] = config.width;
        frame_->data[1]     = dummy.data() + config.width * config.height;
        frame_->linesize[1] = config.width;
        frame_->pts         = 0;

        ret = avcodec_send_frame(ctx_, frame_);
        if (ret < 0) {
            char err[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, err, sizeof(err));
            spdlog::debug("[NvencEncoder] dry-run send_frame failed: {}", err);
            shutdown();
            return false;
        }

        ret = avcodec_receive_packet(ctx_, pkt_);
        // EAGAIN is acceptable (encoder buffering), only real errors fail
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            char err[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, err, sizeof(err));
            spdlog::debug("[NvencEncoder] dry-run receive_packet failed: {}", err);
            shutdown();
            return false;
        }
        av_packet_unref(pkt_);

        // Reset frame pointers (will be set per-encode call)
        frame_->data[0] = nullptr;
        frame_->data[1] = nullptr;
    }

    frame_count_ = 1;  // dry-run consumed frame 0
    spdlog::info("[NvencEncoder] initialized: {}x{} @{} fps, {} kbps, preset=p1, tune=ll",
                 config.width, config.height, config.fps, config.bitrateKbps);
    return true;
}

void NvencEncoder::encode(const uint8_t* nv12Data, size_t dataSize,
                           int64_t timestampUs,
                           std::function<void(const EncodedPacket&)> callback)
{
    if (!ctx_ || !frame_ || !pkt_) return;

    const int ySize  = config_.width * config_.height;
    const int uvSize = config_.width * config_.height / 2;
    if (static_cast<int>(dataSize) < ySize + uvSize) return;

    frame_->data[0]     = const_cast<uint8_t*>(nv12Data);
    frame_->linesize[0] = config_.width;
    frame_->data[1]     = const_cast<uint8_t*>(nv12Data + ySize);
    frame_->linesize[1] = config_.width;
    frame_->pts         = frame_count_++;

    if (force_key_frame_) {
        frame_->pict_type = AV_PICTURE_TYPE_I;
        force_key_frame_ = false;
    } else {
        frame_->pict_type = AV_PICTURE_TYPE_NONE;
    }

    int ret = avcodec_send_frame(ctx_, frame_);
    if (ret < 0) return;

    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        EncodedPacket pkt;
        pkt.data.assign(pkt_->data, pkt_->data + pkt_->size);
        pkt.timestampUs = timestampUs;
        pkt.isKeyFrame  = (pkt_->flags & AV_PKT_FLAG_KEY) != 0;
        callback(pkt);

        av_packet_unref(pkt_);
    }
}

void NvencEncoder::requestKeyFrame()
{
    force_key_frame_ = true;
}

void NvencEncoder::setBitrate(int kbps)
{
    if (!ctx_) return;
    ctx_->bit_rate = static_cast<int64_t>(kbps) * 1000;
    ctx_->rc_max_rate = ctx_->bit_rate;
    config_.bitrateKbps = kbps;
    spdlog::info("[NvencEncoder] bitrate changed to {} kbps", kbps);
}

void NvencEncoder::shutdown()
{
    if (pkt_) {
        av_packet_free(&pkt_);
        pkt_ = nullptr;
    }
    if (frame_) {
        frame_->data[0] = nullptr;
        frame_->data[1] = nullptr;
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (ctx_) {
        avcodec_free_context(&ctx_);
        ctx_ = nullptr;
    }
    spdlog::debug("[NvencEncoder] shutdown");
}

} // namespace hub32agent::encode

#endif // HUB32_WITH_FFMPEG
