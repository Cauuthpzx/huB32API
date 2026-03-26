#pragma once

#include "hub32agent/encode/ColorConverter.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>

namespace hub32agent::encode {

/// @brief D3D11 hardware BGRA->NV12 color converter using VideoProcessor.
/// Uses the GPU's video processing unit for zero-copy color space conversion.
/// Falls back to CpuColorConverter if D3D11 video processor is unavailable.
class D3D11ColorConverter : public ColorConverter
{
public:
    D3D11ColorConverter();
    ~D3D11ColorConverter() override;

    std::string name() const override { return "d3d11-videoproc"; }
    bool initialize(int width, int height) override;
    bool convert(const uint8_t* bgraData, int bgraStride,
                 uint8_t* nv12Out) override;
    size_t nv12BufferSize() const override;
    void shutdown() override;

private:
    void release();

    int width_  = 0;
    int height_ = 0;

    ID3D11Device*            device_       = nullptr;
    ID3D11DeviceContext*     context_      = nullptr;
    ID3D11VideoDevice*       videoDevice_  = nullptr;
    ID3D11VideoContext*      videoCtx_     = nullptr;
    ID3D11VideoProcessor*    processor_    = nullptr;
    ID3D11VideoProcessorEnumerator* enumerator_ = nullptr;

    // Input: BGRA staging texture (CPU upload → GPU)
    ID3D11Texture2D*         inputTex_     = nullptr;
    ID3D11VideoProcessorInputView* inputView_ = nullptr;

    // Output: NV12 texture (GPU → CPU readback)
    ID3D11Texture2D*         outputTex_    = nullptr;
    ID3D11Texture2D*         stagingTex_   = nullptr;
    ID3D11VideoProcessorOutputView* outputView_ = nullptr;
};

} // namespace hub32agent::encode
