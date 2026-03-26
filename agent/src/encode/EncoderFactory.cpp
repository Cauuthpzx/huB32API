/**
 * @file EncoderFactory.cpp
 * @brief Creates the best available H.264 encoder and color converter.
 *
 * Probe order for encoders:
 *   1. NVENC  — requires NVIDIA GPU + nvenc-capable driver
 *   2. QSV    — requires Intel CPU with Quick Sync Video
 *   3. x264   — CPU fallback, always available (requires FFmpeg with libx264)
 *
 * All encoder implementations are conditionally compiled:
 *   - NVENC/QSV/x264 require HUB32_WITH_FFMPEG
 *   - Color converters use libyuv (CPU) or D3D11 compute shader
 */

#include "hub32agent/encode/EncoderFactory.hpp"

#include <spdlog/spdlog.h>

// Encoder implementations — included conditionally
#ifdef HUB32_WITH_FFMPEG
#include "NvencEncoder.hpp"
#include "QsvEncoder.hpp"
#include "X264Encoder.hpp"
#endif
#include "CpuColorConverter.hpp"
#include "D3D11ColorConverter.hpp"

namespace hub32agent::encode {

std::unique_ptr<H264Encoder> EncoderFactory::createBestEncoder(const EncoderConfig& config)
{
    spdlog::info("[EncoderFactory] probing encoders ({}x{} @{} fps, {} kbps)",
                 config.width, config.height, config.fps, config.bitrateKbps);

#ifdef HUB32_WITH_FFMPEG
    // 1. Try NVENC (NVIDIA GPU)
    {
        auto encoder = createEncoder("nvenc", config);
        if (encoder) return encoder;
    }

    // 2. Try QSV (Intel iGPU)
    {
        auto encoder = createEncoder("qsv", config);
        if (encoder) return encoder;
    }

    // 3. Try x264 (CPU fallback)
    {
        auto encoder = createEncoder("x264", config);
        if (encoder) return encoder;
    }
#endif

    spdlog::error("[EncoderFactory] no encoder available"
#ifndef HUB32_WITH_FFMPEG
                  " (HUB32_WITH_FFMPEG not enabled — rebuild with FFmpeg support)"
#endif
    );
    return nullptr;
}

std::unique_ptr<H264Encoder> EncoderFactory::createEncoder(
    const std::string& name, const EncoderConfig& config)
{
#ifdef HUB32_WITH_FFMPEG
    if (name == "nvenc") {
        auto enc = std::make_unique<NvencEncoder>();
        if (enc->initialize(config)) {
            spdlog::info("[EncoderFactory] using NVENC hardware encoder");
            return enc;
        }
        spdlog::info("[EncoderFactory] NVENC not available (no NVIDIA GPU or driver), trying next");
    }
    else if (name == "qsv") {
        auto enc = std::make_unique<QsvEncoder>();
        if (enc->initialize(config)) {
            spdlog::info("[EncoderFactory] using Intel QSV hardware encoder");
            return enc;
        }
        spdlog::info("[EncoderFactory] QSV not available (no Intel GPU or driver), trying next");
    }
    else if (name == "x264") {
        auto enc = std::make_unique<X264Encoder>();
        if (enc->initialize(config)) {
            spdlog::info("[EncoderFactory] using x264 CPU encoder");
            return enc;
        }
        spdlog::warn("[EncoderFactory] x264 encoder initialization failed");
    }
#else
    (void)name;
    (void)config;
#endif

    spdlog::debug("[EncoderFactory] encoder '{}' not available", name);
    return nullptr;
}

std::unique_ptr<ColorConverter> EncoderFactory::createBestConverter(int width, int height)
{
    spdlog::info("[EncoderFactory] probing color converters ({}x{})", width, height);

    // 1. Try D3D11 VideoProcessor (GPU-accelerated BGRA→NV12)
    {
        auto conv = std::make_unique<D3D11ColorConverter>();
        if (conv->initialize(width, height)) {
            spdlog::info("[EncoderFactory] using D3D11 VideoProcessor color converter");
            return conv;
        }
        spdlog::debug("[EncoderFactory] D3D11 color converter not available, trying CPU");
    }

    // 2. CPU manual conversion (always available)
    {
        auto conv = std::make_unique<CpuColorConverter>();
        if (conv->initialize(width, height)) {
            spdlog::info("[EncoderFactory] using CPU manual color converter");
            return conv;
        }
    }

    spdlog::error("[EncoderFactory] no color converter available");
    return nullptr;
}

} // namespace hub32agent::encode
