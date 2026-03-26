#include "CpuColorConverter.hpp"
#include <spdlog/spdlog.h>
#include <cstdint>

namespace hub32agent::encode {

bool CpuColorConverter::initialize(int width, int height)
{
    if (width <= 0 || height <= 0 || (width % 2) != 0 || (height % 2) != 0) {
        spdlog::error("[CpuColorConverter] invalid dimensions {}x{} (must be positive and even)",
                      width, height);
        return false;
    }
    width_  = width;
    height_ = height;
    spdlog::info("[CpuColorConverter] initialized for {}x{}", width, height);
    return true;
}

bool CpuColorConverter::convert(const uint8_t* bgraData, int bgraStride,
                                 uint8_t* nv12Out)
{
    if (!bgraData || !nv12Out || width_ <= 0 || height_ <= 0) return false;

    uint8_t* yPlane  = nv12Out;
    uint8_t* uvPlane = nv12Out + width_ * height_;

    // BT.601 coefficients for RGB->YUV conversion (fixed-point, shift 8):
    //   Y  =  (( 66 * R + 129 * G +  25 * B + 128) >> 8) + 16
    //   U  =  ((-38 * R -  74 * G + 112 * B + 128) >> 8) + 128
    //   V  =  ((112 * R -  94 * G -  18 * B + 128) >> 8) + 128

    for (int y = 0; y < height_; ++y) {
        const uint8_t* row = bgraData + y * bgraStride;
        for (int x = 0; x < width_; ++x) {
            // BGRA layout: B, G, R, A
            const uint8_t b = row[x * 4 + 0];
            const uint8_t g = row[x * 4 + 1];
            const uint8_t r = row[x * 4 + 2];

            // Y (luma)
            int yVal = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            yPlane[y * width_ + x] = static_cast<uint8_t>(
                yVal < 0 ? 0 : (yVal > 255 ? 255 : yVal));

            // UV (chroma) — subsample 2x2: only compute for top-left pixel of each block
            if ((x & 1) == 0 && (y & 1) == 0) {
                int uVal = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int vVal = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                int uvIdx = (y / 2) * width_ + x;
                uvPlane[uvIdx + 0] = static_cast<uint8_t>(
                    uVal < 0 ? 0 : (uVal > 255 ? 255 : uVal));
                uvPlane[uvIdx + 1] = static_cast<uint8_t>(
                    vVal < 0 ? 0 : (vVal > 255 ? 255 : vVal));
            }
        }
    }

    return true;
}

size_t CpuColorConverter::nv12BufferSize() const
{
    // NV12: Y plane (width * height) + UV plane (width * height / 2)
    return static_cast<size_t>(width_) * height_ * 3 / 2;
}

void CpuColorConverter::shutdown()
{
    width_  = 0;
    height_ = 0;
}

} // namespace hub32agent::encode
