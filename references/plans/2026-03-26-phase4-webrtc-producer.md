# Phase 4: Agent H.264 WebRTC Producer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add H.264 encoding and WebRTC streaming to the agent binary. The agent captures the screen via DXGI, encodes to H.264 (NVENC → QSV → x264 fallback), and sends RTP packets via libdatachannel WebRTC transport to the mediasoup SFU. Simulcast 3 layers for thumbnail/medium/fullscreen quality.

**Architecture:** Pipeline: DXGI AcquireNextFrame → GPU Texture (BGRA) → NV12 conversion (D3D11 compute shader or libyuv) → H.264 encode (NVENC/QSV/x264) → RTP packetize → libdatachannel PeerConnection → mediasoup Router.

**Tech Stack:** C++17, FFmpeg libavcodec (NVENC, QSV, x264), libdatachannel, libyuv (color conversion), DXGI

**Depends on:** Phase 3 (mediasoup SFU running, signaling API exists)

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Create | `agent/include/hub32agent/media/H264Encoder.hpp` | Abstract encoder interface |
| Create | `agent/src/media/H264Encoder.cpp` | NVENC → QSV → x264 fallback chain |
| Create | `agent/include/hub32agent/media/ColorConverter.hpp` | BGRA → NV12 conversion |
| Create | `agent/src/media/ColorConverter.cpp` | D3D11 compute shader or libyuv |
| Create | `agent/include/hub32agent/media/WebRtcProducer.hpp` | libdatachannel WebRTC producer |
| Create | `agent/src/media/WebRtcProducer.cpp` | Implementation |
| Create | `agent/include/hub32agent/media/StreamPipeline.hpp` | Orchestrates capture→encode→stream |
| Create | `agent/src/media/StreamPipeline.cpp` | Implementation |
| Modify | `agent/src/main.cpp` | Start StreamPipeline after registration |
| Modify | `agent/CMakeLists.txt` | Add FFmpeg, libdatachannel, libyuv deps |
| Create | `cmake/deps/FindFFmpeg.cmake` | Find FFmpeg libraries |
| Create | `cmake/deps/FindLibDataChannel.cmake` | Find libdatachannel |

---

### Task 1: Add FFmpeg and libdatachannel Dependencies

- [ ] **Step 1: Install FFmpeg development libraries**

```bash
# MSYS2/MinGW64
pacman -S mingw-w64-x86_64-ffmpeg
# Provides: libavcodec, libavutil, libavformat, libswscale
```

- [ ] **Step 2: Install libdatachannel**

```bash
# Option A: MSYS2 package (if available)
pacman -S mingw-w64-x86_64-libdatachannel

# Option B: Build from source
git submodule add https://github.com/paullouisageneau/libdatachannel.git \
    third_party/libdatachannel
cd third_party/libdatachannel
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_GNUTLS=0 -DUSE_NICE=0
cmake --build build
```

- [ ] **Step 3: Create FindFFmpeg.cmake and FindLibDataChannel.cmake**
- [ ] **Step 4: Update agent/CMakeLists.txt with new dependencies**
- [ ] **Step 5: Verify build compiles**
- [ ] **Step 6: Commit**

---

### Task 2: H264Encoder (NVENC → QSV → x264 Fallback)

**Files:**
- Create: `agent/include/hub32agent/media/H264Encoder.hpp`
- Create: `agent/src/media/H264Encoder.cpp`

- [ ] **Step 1: Write H264Encoder header**

```cpp
#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace hub32agent::media {

struct EncoderConfig {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrateKbps = 2500;
    std::string preset = "ultrafast"; // for x264 fallback
};

struct EncodedFrame {
    std::vector<uint8_t> data;      // H.264 NAL units
    bool isKeyframe = false;
    int64_t pts = 0;                // presentation timestamp (ms)
    int64_t dts = 0;                // decode timestamp (ms)
};

class H264Encoder {
public:
    explicit H264Encoder(const EncoderConfig& cfg);
    ~H264Encoder();

    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    /// Encode a single NV12 frame
    /// Returns encoded NAL units, empty if frame was buffered
    EncodedFrame encode(const uint8_t* nv12Data, int width, int height, int64_t pts);

    /// Force a keyframe on next encode
    void forceKeyframe();

    /// Update bitrate dynamically
    void setBitrate(int kbps);

    /// Which encoder is active?
    std::string activeEncoder() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hub32agent::media
```

- [ ] **Step 2: Write implementation with fallback chain**

1. Try `h264_nvenc` (NVIDIA GPU)
2. Fallback `h264_qsv` (Intel iGPU)
3. Fallback `libx264` (CPU, preset=ultrafast)

Use FFmpeg `avcodec_open2()` with each codec name. First one that succeeds wins.

- [ ] **Step 3: Write test**
- [ ] **Step 4: Build and verify**
- [ ] **Step 5: Commit**

---

### Task 3: ColorConverter (BGRA → NV12)

**Files:**
- Create: `agent/include/hub32agent/media/ColorConverter.hpp`
- Create: `agent/src/media/ColorConverter.cpp`

**Why:** DXGI outputs BGRA textures. H.264 encoders need NV12.

- [ ] **Step 1: Write ColorConverter using libyuv or swscale**

```cpp
class ColorConverter {
public:
    ColorConverter(int width, int height);

    /// Convert BGRA frame to NV12
    /// Returns NV12 buffer (Y plane + UV interleaved)
    std::vector<uint8_t> bgraToNv12(const uint8_t* bgraData, int stride);

private:
    int m_width, m_height;
    std::vector<uint8_t> m_nv12Buffer;
    // Optional: SwsContext* for FFmpeg swscale
};
```

- [ ] **Step 2: Write implementation using FFmpeg swscale (simplest, no extra deps)**
- [ ] **Step 3: Test with known BGRA→NV12 conversion**
- [ ] **Step 4: Commit**

---

### Task 4: WebRtcProducer (libdatachannel)

**Files:**
- Create: `agent/include/hub32agent/media/WebRtcProducer.hpp`
- Create: `agent/src/media/WebRtcProducer.cpp`

- [ ] **Step 1: Write WebRtcProducer header**

```cpp
class WebRtcProducer {
public:
    struct Config {
        std::string serverUrl;     // hub32api REST URL
        std::string authToken;     // JWT
        std::string locationId;    // room ID for Router selection
        std::vector<std::string> iceServers; // STUN/TURN URLs
    };

    explicit WebRtcProducer(const Config& cfg);
    ~WebRtcProducer();

    /// Connect to SFU: create transport, DTLS handshake, create producer
    bool connect();

    /// Send an encoded H.264 frame as RTP
    void sendFrame(const EncodedFrame& frame);

    /// Disconnect and cleanup
    void disconnect();

    bool isConnected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
```

- [ ] **Step 2: Write implementation**

Flow:
1. `GET /api/v1/stream/ice-servers` → get STUN/TURN URLs
2. `GET /api/v1/stream/capabilities/:locationId` → get Router RTP capabilities
3. `POST /api/v1/stream/transport` → get transport ID + ICE + DTLS params
4. Create libdatachannel PeerConnection with ICE candidates
5. `POST /api/v1/stream/transport/:id/connect` → send DTLS parameters
6. `POST /api/v1/stream/produce` → get producer ID
7. Send H.264 RTP packets via the PeerConnection track

- [ ] **Step 3: Auto-reconnect logic**
- [ ] **Step 4: Test with mock server**
- [ ] **Step 5: Commit**

---

### Task 5: StreamPipeline (Orchestrator)

**Files:**
- Create: `agent/include/hub32agent/media/StreamPipeline.hpp`
- Create: `agent/src/media/StreamPipeline.cpp`

- [ ] **Step 1: Write StreamPipeline**

```cpp
class StreamPipeline {
public:
    struct Config {
        int captureWidth = 1920;
        int captureHeight = 1080;
        int fps = 30;
        int bitrateKbps = 2500;
        std::string serverUrl;
        std::string authToken;
        std::string locationId;
    };

    explicit StreamPipeline(const Config& cfg);
    ~StreamPipeline();

    /// Start the capture→encode→stream pipeline in a background thread
    void start();

    /// Stop the pipeline
    void stop();

    bool isRunning() const;

private:
    void pipelineLoop();
    // ScreenCapture → ColorConverter → H264Encoder → WebRtcProducer
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
```

The pipeline loop:
1. `ScreenCapture::capture()` → BGRA frame (existing code)
2. `ColorConverter::bgraToNv12()` → NV12 frame
3. `H264Encoder::encode()` → H.264 NAL units
4. `WebRtcProducer::sendFrame()` → RTP to SFU
5. Sleep to maintain target FPS

- [ ] **Step 2: Integrate into agent main.cpp**

After successful registration:
```cpp
// Start streaming pipeline
StreamPipeline::Config streamCfg;
streamCfg.serverUrl = config.serverUrl;
streamCfg.authToken = agentToken;
streamCfg.locationId = assignedLocationId;
auto pipeline = std::make_unique<StreamPipeline>(streamCfg);
pipeline->start();
```

- [ ] **Step 3: Build and verify**
- [ ] **Step 4: Commit**

---

### Task 6: Simulcast Support (3 Layers)

- [ ] **Step 1: Create 3 encoders with different bitrates**

```
Low:    320x180,  150 kbps (thumbnail)
Medium: 640x360,  500 kbps (medium view)
High:   1280x720, 2500 kbps (fullscreen)
```

- [ ] **Step 2: Downscale captured frame for each layer**
- [ ] **Step 3: Each layer → separate RTP stream → separate Producer**
- [ ] **Step 4: Test with mediasoup consumer selecting layers**
- [ ] **Step 5: Commit**

---

## Phase 4 Completion Checklist

- [ ] FFmpeg libavcodec integrated into agent build
- [ ] NVENC → QSV → x264 fallback chain works
- [ ] BGRA → NV12 color conversion works
- [ ] libdatachannel WebRTC producer connects to mediasoup
- [ ] StreamPipeline captures → encodes → streams H.264
- [ ] Simulcast 3 layers (150k/500k/2500k)
- [ ] Auto-reconnect on network interruption
- [ ] Agent binary builds and runs with streaming
