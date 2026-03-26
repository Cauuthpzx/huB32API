#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hub32agent::encode {

// -----------------------------------------------------------------------
// ColorConverter — converts BGRA frames (from DXGI/GDI) to NV12 for H.264 encoding
//
// Implementations:
//   CpuColorConverter   — libyuv-based, always available (CPU fallback)
//   D3D11ColorConverter — D3D11 compute shader, zero-copy from DXGI texture
// -----------------------------------------------------------------------
class ColorConverter
{
public:
    virtual ~ColorConverter() = default;

    /// @brief Returns the converter name (e.g., "cpu-libyuv", "d3d11-compute").
    virtual std::string name() const = 0;

    /// @brief Initializes the converter for the given frame dimensions.
    /// @param width  Frame width in pixels.
    /// @param height Frame height in pixels.
    /// @return true on success, false if this converter is not available.
    virtual bool initialize(int width, int height) = 0;

    /// @brief Converts a BGRA frame to NV12 format.
    /// @param bgraData   Pointer to BGRA pixel data (4 bytes per pixel, top-to-bottom).
    /// @param bgraStride Row stride in bytes (typically width * 4).
    /// @param nv12Out    Output buffer for NV12 data. Must be pre-allocated to
    ///                   at least width * height * 3 / 2 bytes.
    /// @return true on success.
    virtual bool convert(const uint8_t* bgraData, int bgraStride,
                         uint8_t* nv12Out) = 0;

    /// @brief Returns the required NV12 buffer size for the configured dimensions.
    virtual size_t nv12BufferSize() const = 0;

    /// @brief Releases converter resources.
    virtual void shutdown() = 0;
};

} // namespace hub32agent::encode
