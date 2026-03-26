#include "D3D11ColorConverter.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

namespace hub32agent::encode {

D3D11ColorConverter::D3D11ColorConverter() = default;

D3D11ColorConverter::~D3D11ColorConverter()
{
    shutdown();
}

bool D3D11ColorConverter::initialize(int width, int height)
{
    if (width <= 0 || height <= 0 || (width % 2) != 0 || (height % 2) != 0) {
        spdlog::error("[D3D11ColorConverter] invalid dimensions {}x{}", width, height);
        return false;
    }

    width_  = width;
    height_ = height;

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT, // required for VideoProcessor
        nullptr, 0, D3D11_SDK_VERSION,
        &device_, &featureLevel, &context_);

    if (FAILED(hr) || !device_) {
        spdlog::debug("[D3D11ColorConverter] D3D11CreateDevice failed: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    // Get video device + context interfaces
    hr = device_->QueryInterface(__uuidof(ID3D11VideoDevice),
                                  reinterpret_cast<void**>(&videoDevice_));
    if (FAILED(hr) || !videoDevice_) {
        spdlog::debug("[D3D11ColorConverter] no ID3D11VideoDevice support");
        release();
        return false;
    }

    hr = context_->QueryInterface(__uuidof(ID3D11VideoContext),
                                   reinterpret_cast<void**>(&videoCtx_));
    if (FAILED(hr) || !videoCtx_) {
        spdlog::debug("[D3D11ColorConverter] no ID3D11VideoContext support");
        release();
        return false;
    }

    // Create video processor enumerator
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc{};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputWidth       = static_cast<UINT>(width);
    contentDesc.InputHeight      = static_cast<UINT>(height);
    contentDesc.OutputWidth      = static_cast<UINT>(width);
    contentDesc.OutputHeight     = static_cast<UINT>(height);
    contentDesc.Usage            = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    hr = videoDevice_->CreateVideoProcessorEnumerator(&contentDesc, &enumerator_);
    if (FAILED(hr) || !enumerator_) {
        spdlog::debug("[D3D11ColorConverter] CreateVideoProcessorEnumerator failed: 0x{:08X}",
                      static_cast<unsigned>(hr));
        release();
        return false;
    }

    // Create video processor
    hr = videoDevice_->CreateVideoProcessor(enumerator_, 0, &processor_);
    if (FAILED(hr) || !processor_) {
        spdlog::debug("[D3D11ColorConverter] CreateVideoProcessor failed: 0x{:08X}",
                      static_cast<unsigned>(hr));
        release();
        return false;
    }

    // Create input texture (BGRA, CPU writable → GPU readable)
    D3D11_TEXTURE2D_DESC inputDesc{};
    inputDesc.Width       = static_cast<UINT>(width);
    inputDesc.Height      = static_cast<UINT>(height);
    inputDesc.MipLevels   = 1;
    inputDesc.ArraySize    = 1;
    inputDesc.Format       = DXGI_FORMAT_B8G8R8A8_UNORM;
    inputDesc.SampleDesc   = {1, 0};
    inputDesc.Usage        = D3D11_USAGE_DEFAULT;
    inputDesc.BindFlags    = D3D11_BIND_RENDER_TARGET;

    hr = device_->CreateTexture2D(&inputDesc, nullptr, &inputTex_);
    if (FAILED(hr) || !inputTex_) {
        spdlog::debug("[D3D11ColorConverter] input texture creation failed: 0x{:08X}",
                      static_cast<unsigned>(hr));
        release();
        return false;
    }

    // Create input view
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc{};
    inputViewDesc.FourCC         = 0;
    inputViewDesc.ViewDimension  = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputViewDesc.Texture2D.MipSlice = 0;

    hr = videoDevice_->CreateVideoProcessorInputView(
        inputTex_, enumerator_, &inputViewDesc, &inputView_);
    if (FAILED(hr) || !inputView_) {
        spdlog::debug("[D3D11ColorConverter] input view creation failed: 0x{:08X}",
                      static_cast<unsigned>(hr));
        release();
        return false;
    }

    // Create output texture (NV12, GPU only)
    D3D11_TEXTURE2D_DESC outputDesc{};
    outputDesc.Width       = static_cast<UINT>(width);
    outputDesc.Height      = static_cast<UINT>(height);
    outputDesc.MipLevels   = 1;
    outputDesc.ArraySize    = 1;
    outputDesc.Format       = DXGI_FORMAT_NV12;
    outputDesc.SampleDesc   = {1, 0};
    outputDesc.Usage        = D3D11_USAGE_DEFAULT;
    outputDesc.BindFlags    = D3D11_BIND_RENDER_TARGET;

    hr = device_->CreateTexture2D(&outputDesc, nullptr, &outputTex_);
    if (FAILED(hr) || !outputTex_) {
        spdlog::debug("[D3D11ColorConverter] output NV12 texture creation failed: 0x{:08X}",
                      static_cast<unsigned>(hr));
        release();
        return false;
    }

    // Create output view
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc{};
    outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputViewDesc.Texture2D.MipSlice = 0;

    hr = videoDevice_->CreateVideoProcessorOutputView(
        outputTex_, enumerator_, &outputViewDesc, &outputView_);
    if (FAILED(hr) || !outputView_) {
        spdlog::debug("[D3D11ColorConverter] output view creation failed: 0x{:08X}",
                      static_cast<unsigned>(hr));
        release();
        return false;
    }

    // Create staging texture for CPU readback of NV12 result
    D3D11_TEXTURE2D_DESC stagingDesc{};
    stagingDesc.Width       = static_cast<UINT>(width);
    stagingDesc.Height      = static_cast<UINT>(height);
    stagingDesc.MipLevels   = 1;
    stagingDesc.ArraySize    = 1;
    stagingDesc.Format       = DXGI_FORMAT_NV12;
    stagingDesc.SampleDesc   = {1, 0};
    stagingDesc.Usage        = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = device_->CreateTexture2D(&stagingDesc, nullptr, &stagingTex_);
    if (FAILED(hr) || !stagingTex_) {
        spdlog::debug("[D3D11ColorConverter] staging texture creation failed: 0x{:08X}",
                      static_cast<unsigned>(hr));
        release();
        return false;
    }

    spdlog::info("[D3D11ColorConverter] initialized for {}x{} (GPU VideoProcessor)", width, height);
    return true;
}

bool D3D11ColorConverter::convert(const uint8_t* bgraData, int bgraStride,
                                   uint8_t* nv12Out)
{
    if (!bgraData || !nv12Out || !context_ || !processor_) return false;

    // Upload BGRA data to input texture
    context_->UpdateSubresource(inputTex_, 0, nullptr,
                                bgraData, static_cast<UINT>(bgraStride), 0);

    // Run video processor: BGRA → NV12
    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable    = TRUE;
    stream.pInputSurface = inputView_;

    HRESULT hr = videoCtx_->VideoProcessorBlt(processor_, outputView_, 0, 1, &stream);
    if (FAILED(hr)) {
        spdlog::warn("[D3D11ColorConverter] VideoProcessorBlt failed: 0x{:08X}",
                     static_cast<unsigned>(hr));
        return false;
    }

    // Copy output to staging for CPU readback
    context_->CopyResource(stagingTex_, outputTex_);

    // Map staging texture and copy NV12 data to output buffer
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context_->Map(stagingTex_, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        spdlog::warn("[D3D11ColorConverter] Map staging failed: 0x{:08X}",
                     static_cast<unsigned>(hr));
        return false;
    }

    // Copy Y plane
    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < height_; ++y) {
        std::memcpy(nv12Out + y * width_, src + y * mapped.RowPitch, width_);
    }

    // Copy UV plane (height/2 rows, width bytes each)
    const auto* uvSrc = src + mapped.RowPitch * height_;
    uint8_t* uvDst = nv12Out + width_ * height_;
    for (int y = 0; y < height_ / 2; ++y) {
        std::memcpy(uvDst + y * width_, uvSrc + y * mapped.RowPitch, width_);
    }

    context_->Unmap(stagingTex_, 0);
    return true;
}

size_t D3D11ColorConverter::nv12BufferSize() const
{
    return static_cast<size_t>(width_) * height_ * 3 / 2;
}

void D3D11ColorConverter::shutdown()
{
    release();
    width_  = 0;
    height_ = 0;
}

void D3D11ColorConverter::release()
{
    if (outputView_)  { outputView_->Release();  outputView_  = nullptr; }
    if (stagingTex_)  { stagingTex_->Release();  stagingTex_  = nullptr; }
    if (outputTex_)   { outputTex_->Release();   outputTex_   = nullptr; }
    if (inputView_)   { inputView_->Release();   inputView_   = nullptr; }
    if (inputTex_)    { inputTex_->Release();    inputTex_    = nullptr; }
    if (processor_)   { processor_->Release();   processor_   = nullptr; }
    if (enumerator_)  { enumerator_->Release();  enumerator_  = nullptr; }
    if (videoCtx_)    { videoCtx_->Release();    videoCtx_    = nullptr; }
    if (videoDevice_) { videoDevice_->Release(); videoDevice_ = nullptr; }
    if (context_)     { context_->Release();     context_     = nullptr; }
    if (device_)      { device_->Release();      device_      = nullptr; }
}

} // namespace hub32agent::encode
