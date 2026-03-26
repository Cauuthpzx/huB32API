#pragma once

#include "H264Encoder.hpp"
#include "ColorConverter.hpp"
#include <memory>
#include <string>

namespace hub32agent::encode {

// -----------------------------------------------------------------------
// EncoderFactory — creates the best available encoder and color converter
//
// Encoder probe order (first success wins):
//   1. NVENC  (NVIDIA GPU — lowest latency, hardware H.264)
//   2. QSV    (Intel iGPU — Quick Sync Video)
//   3. x264   (CPU — ultrafast preset, always available)
//
// Color converter probe order:
//   1. D3D11 compute shader (zero-copy from DXGI texture, needs DX11 GPU)
//   2. CPU libyuv (always available)
// -----------------------------------------------------------------------
class EncoderFactory
{
public:
    /// @brief Creates the best available H.264 encoder.
    /// Probes NVENC → QSV → x264 in order. Returns nullptr if none available.
    /// @param config Encoder configuration (width, height, fps, bitrate).
    /// @return Initialized encoder, or nullptr on total failure.
    static std::unique_ptr<H264Encoder> createBestEncoder(const EncoderConfig& config);

    /// @brief Creates a specific encoder by name.
    /// @param name    "nvenc", "qsv", or "x264".
    /// @param config  Encoder configuration.
    /// @return Initialized encoder, or nullptr if not available.
    static std::unique_ptr<H264Encoder> createEncoder(const std::string& name,
                                                       const EncoderConfig& config);

    /// @brief Creates the best available color converter.
    /// Probes D3D11 → CPU in order.
    /// @param width  Frame width in pixels.
    /// @param height Frame height in pixels.
    /// @return Initialized converter, or nullptr on total failure.
    static std::unique_ptr<ColorConverter> createBestConverter(int width, int height);

    EncoderFactory() = delete;
};

} // namespace hub32agent::encode
