#pragma once

#include "hub32agent/encode/ColorConverter.hpp"

namespace hub32agent::encode {

/// @brief CPU-based BGRA->NV12 color converter.
/// Uses manual BT.601 fixed-point conversion (no libyuv dependency).
/// Always available as a fallback.
class CpuColorConverter : public ColorConverter
{
public:
    std::string name() const override { return "cpu-manual"; }
    bool initialize(int width, int height) override;
    bool convert(const uint8_t* bgraData, int bgraStride,
                 uint8_t* nv12Out) override;
    size_t nv12BufferSize() const override;
    void shutdown() override;

private:
    int width_  = 0;
    int height_ = 0;
};

} // namespace hub32agent::encode
