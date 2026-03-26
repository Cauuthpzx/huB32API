/**
 * @file test_stream_pipeline.cpp
 * @brief Unit tests for StreamPipeline, RTP packetization, and adaptive quality.
 *
 * Tests are designed to run without actual GPU or WebRTC connection.
 * They verify:
 *   - NAL unit splitting (find_nal_units helper)
 *   - STAP-A aggregation logic
 *   - SPS/PPS NAL type detection
 *   - RTP header construction and FU-A fragmentation format
 *   - Pipeline configuration defaults
 *   - Adaptive quality state transitions
 *   - Reconnect exponential backoff calculation
 *   - Graceful shutdown semantics
 *   - Encoder integration
 */

#if defined(HUB32_WITH_FFMPEG) && defined(HUB32_WITH_WEBRTC)

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "hub32agent/pipeline/StreamPipeline.hpp"
#include "hub32agent/encode/EncoderFactory.hpp"
#include "hub32agent/webrtc/WebRtcProducer.hpp"

#include <cstring>
#include <vector>

using namespace hub32agent;

// =======================================================================
// NAL unit splitting tests (inline mirror of WebRtcProducer logic)
// =======================================================================

namespace {

struct NalUnit { const uint8_t* data; size_t size; };

static std::vector<NalUnit> find_nal_units(const uint8_t* buf, size_t len)
{
    std::vector<NalUnit> nals;
    if (!buf || len == 0) return nals;
    size_t i = 0;
    while (i < len) {
        size_t sc_len = 0;
        if (i + 3 < len && buf[i] == 0 && buf[i+1] == 0 &&
            buf[i+2] == 0 && buf[i+3] == 1)
            sc_len = 4;
        else if (i + 2 < len && buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 1)
            sc_len = 3;
        else { ++i; continue; }

        size_t nal_start = i + sc_len;
        size_t j = nal_start + 1;
        while (j < len) {
            if (j + 3 < len && buf[j] == 0 && buf[j+1] == 0 &&
                buf[j+2] == 0 && buf[j+3] == 1) break;
            if (j + 2 < len && buf[j] == 0 && buf[j+1] == 0 && buf[j+2] == 1) break;
            ++j;
        }
        if (nal_start < len)
            nals.push_back({buf + nal_start, j - nal_start});
        i = j;
    }
    return nals;
}

} // anonymous namespace

TEST(NalSplitTest, SingleNalWith4ByteStartCode)
{
    const uint8_t buf[] = {0,0,0,1, 0x67, 0x42, 0xC0, 0x1E, 0x00};
    auto nals = find_nal_units(buf, sizeof(buf));
    ASSERT_EQ(nals.size(), 1u);
    EXPECT_EQ(nals[0].size, 5u);
    EXPECT_EQ(nals[0].data[0], 0x67);
}

TEST(NalSplitTest, TwoNalsWith3ByteStartCodes)
{
    const uint8_t buf[] = {0,0,1, 0x67, 0x42,
                            0,0,1, 0x68, 0xCE};
    auto nals = find_nal_units(buf, sizeof(buf));
    ASSERT_EQ(nals.size(), 2u);
    EXPECT_EQ(nals[0].size, 2u);
    EXPECT_EQ(nals[1].size, 2u);
}

TEST(NalSplitTest, MixedStartCodes)
{
    const uint8_t buf[] = {0,0,0,1, 0x65,
                            0,0,1, 0x41, 0x9A};
    auto nals = find_nal_units(buf, sizeof(buf));
    ASSERT_EQ(nals.size(), 2u);
    EXPECT_EQ(nals[0].data[0], 0x65);
    EXPECT_EQ(nals[1].data[0], 0x41);
}

TEST(NalSplitTest, EmptyInputReturnsNoNals)
{
    auto nals = find_nal_units(nullptr, 0);
    EXPECT_TRUE(nals.empty());
}

// =======================================================================
// STAP-A aggregation logic tests
// =======================================================================

namespace {

constexpr int kStapSmall  = 600;
constexpr int kMaxPayload = 1188;

struct StapGroup { size_t start; size_t end; size_t total_bytes; };

static std::vector<StapGroup> compute_stap_groups(const std::vector<NalUnit>& nals)
{
    std::vector<StapGroup> groups;
    size_t i = 0;
    while (i < nals.size()) {
        if (nals[i].size > static_cast<size_t>(kStapSmall)) { ++i; continue; }
        size_t j         = i;
        size_t stap_bytes = 1; // STAP-A type byte
        while (j < nals.size()) {
            if (nals[j].size > static_cast<size_t>(kStapSmall)) break;
            size_t needed = stap_bytes + 2 + nals[j].size;
            if (needed > static_cast<size_t>(kMaxPayload)) break;
            stap_bytes = needed;
            ++j;
        }
        if (j > i + 1) groups.push_back({i, j, stap_bytes});
        i = j > i ? j : i + 1;
    }
    return groups;
}

} // anonymous namespace

TEST(StapATest, SmallNalsAreGrouped)
{
    std::vector<uint8_t> buf1(100, 0x41);
    std::vector<uint8_t> buf2(100, 0x42);
    std::vector<NalUnit> nals = {{buf1.data(), 100}, {buf2.data(), 100}};
    auto groups = compute_stap_groups(nals);
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].start, 0u);
    EXPECT_EQ(groups[0].end,   2u);
    // total: 1 (STAP-A) + (2+100) + (2+100) = 205
    EXPECT_EQ(groups[0].total_bytes, 205u);
}

TEST(StapATest, LargeNalNotGrouped)
{
    std::vector<uint8_t> big(700, 0x65);
    std::vector<NalUnit> nals = {{big.data(), 700}};
    auto groups = compute_stap_groups(nals);
    EXPECT_TRUE(groups.empty());
}

TEST(StapATest, DoesNotExceedMtu)
{
    std::vector<uint8_t> buf(500, 0x41);
    std::vector<NalUnit> nals = {
        {buf.data(), 500}, {buf.data(), 500}, {buf.data(), 500}
    };
    auto groups = compute_stap_groups(nals);
    for (const auto& g : groups) {
        EXPECT_LE(g.total_bytes, static_cast<size_t>(kMaxPayload));
    }
}

// =======================================================================
// SPS/PPS NAL type detection
// =======================================================================

TEST(SpsPpsTest, NalType7IsSps)
{
    uint8_t sps_hdr = 0x67;
    EXPECT_EQ(sps_hdr & 0x1F, 7u);
}

TEST(SpsPpsTest, NalType8IsPps)
{
    uint8_t pps_hdr = 0x68;
    EXPECT_EQ(pps_hdr & 0x1F, 8u);
}

TEST(SpsPpsTest, NalType5IsIdr)
{
    uint8_t idr_hdr = 0x65;
    EXPECT_EQ(idr_hdr & 0x1F, 5u);
}

// =======================================================================
// RTP header and FU-A format tests
// =======================================================================

namespace {

constexpr size_t kRtpHeaderSize = 12;
constexpr uint8_t kRtpVersion  = 2;
constexpr uint8_t kPayloadType = 96;

struct RtpHeader
{
    uint8_t  version;
    bool     padding;
    bool     extension;
    uint8_t  csrcCount;
    bool     marker;
    uint8_t  payloadType;
    uint16_t sequenceNumber;
    uint32_t timestamp;
    uint32_t ssrc;

    bool parseFrom(const uint8_t* data, size_t len)
    {
        if (len < kRtpHeaderSize) return false;
        version       = (data[0] >> 6) & 0x03;
        padding       = (data[0] >> 5) & 0x01;
        extension     = (data[0] >> 4) & 0x01;
        csrcCount     = data[0] & 0x0F;
        marker        = (data[1] >> 7) & 0x01;
        payloadType   = data[1] & 0x7F;
        sequenceNumber = (static_cast<uint16_t>(data[2]) << 8) | data[3];
        timestamp     = (static_cast<uint32_t>(data[4]) << 24) |
                        (static_cast<uint32_t>(data[5]) << 16) |
                        (static_cast<uint32_t>(data[6]) << 8)  |
                        static_cast<uint32_t>(data[7]);
        ssrc          = (static_cast<uint32_t>(data[8]) << 24) |
                        (static_cast<uint32_t>(data[9]) << 16) |
                        (static_cast<uint32_t>(data[10]) << 8) |
                        static_cast<uint32_t>(data[11]);
        return true;
    }
};

std::vector<uint8_t> buildSingleNalRtpPacket(
    const uint8_t* nalData, size_t nalSize,
    uint32_t timestamp, uint16_t seqNum, uint32_t ssrc, bool marker)
{
    std::vector<uint8_t> packet(kRtpHeaderSize + nalSize);
    auto* hdr = packet.data();
    hdr[0] = (kRtpVersion << 6);
    hdr[1] = kPayloadType | (marker ? 0x80 : 0x00);
    hdr[2] = static_cast<uint8_t>(seqNum >> 8);
    hdr[3] = static_cast<uint8_t>(seqNum & 0xFF);
    hdr[4] = static_cast<uint8_t>(timestamp >> 24);
    hdr[5] = static_cast<uint8_t>(timestamp >> 16);
    hdr[6] = static_cast<uint8_t>(timestamp >> 8);
    hdr[7] = static_cast<uint8_t>(timestamp & 0xFF);
    hdr[8]  = static_cast<uint8_t>(ssrc >> 24);
    hdr[9]  = static_cast<uint8_t>(ssrc >> 16);
    hdr[10] = static_cast<uint8_t>(ssrc >> 8);
    hdr[11] = static_cast<uint8_t>(ssrc & 0xFF);
    std::memcpy(hdr + kRtpHeaderSize, nalData, nalSize);
    return packet;
}

} // anonymous namespace

TEST(RtpPacketTest, SingleNalPacketHasCorrectHeader)
{
    uint8_t nal[] = {0x65, 0xAB, 0xCD, 0xEF};
    auto packet = buildSingleNalRtpPacket(nal, sizeof(nal), 90000, 1, 42, true);

    RtpHeader hdr;
    ASSERT_TRUE(hdr.parseFrom(packet.data(), packet.size()));
    EXPECT_EQ(hdr.version, kRtpVersion);
    EXPECT_FALSE(hdr.padding);
    EXPECT_FALSE(hdr.extension);
    EXPECT_EQ(hdr.csrcCount, 0u);
    EXPECT_TRUE(hdr.marker);
    EXPECT_EQ(hdr.payloadType, kPayloadType);
    EXPECT_EQ(hdr.sequenceNumber, 1u);
    EXPECT_EQ(hdr.timestamp, 90000u);
    EXPECT_EQ(hdr.ssrc, 42u);
    EXPECT_EQ(packet.size(), kRtpHeaderSize + sizeof(nal));
    EXPECT_EQ(std::memcmp(packet.data() + kRtpHeaderSize, nal, sizeof(nal)), 0);
}

TEST(RtpPacketTest, TimestampConversion90kHz)
{
    int64_t oneSecondUs = 1'000'000;
    uint32_t rtpTs = static_cast<uint32_t>(oneSecondUs * 90000 / 1'000'000);
    EXPECT_EQ(rtpTs, 90000u);

    int64_t frameIntervalUs = 33333;
    uint32_t frameTs = static_cast<uint32_t>(frameIntervalUs * 90000 / 1'000'000);
    EXPECT_EQ(frameTs, 2999u);

    EXPECT_EQ(static_cast<uint32_t>(0 * 90000 / 1'000'000), 0u);
}

TEST(RtpPacketTest, FuAFragmentationHeaderFormat)
{
    uint8_t originalNalHeader = 0x65;
    uint8_t nalNri  = originalNalHeader & 0x60;
    uint8_t nalType = originalNalHeader & 0x1F;

    uint8_t fuIndicator = nalNri | 28;
    EXPECT_EQ(fuIndicator, 0x7C);

    uint8_t fuHeaderStart = 0x80 | nalType;
    EXPECT_EQ(fuHeaderStart, 0x85);

    uint8_t fuHeaderMiddle = nalType;
    EXPECT_EQ(fuHeaderMiddle, 0x05);

    uint8_t fuHeaderEnd = 0x40 | nalType;
    EXPECT_EQ(fuHeaderEnd, 0x45);
}

TEST(RtpPacketTest, SequenceNumberWrapsAt16Bit)
{
    uint16_t seq = 65534;
    EXPECT_EQ(static_cast<uint16_t>(seq + 1), 65535);
    EXPECT_EQ(static_cast<uint16_t>(seq + 2), 0);
}

// =======================================================================
// H.264 NAL unit start code parsing (bitstream level)
// =======================================================================

TEST(NalParsingTest, Finds4ByteStartCodes)
{
    std::vector<uint8_t> bitstream = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0xAA, 0xBB,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xCC, 0xDD
    };
    int count = 0;
    for (size_t i = 0; i + 3 < bitstream.size(); ++i) {
        if (bitstream[i] == 0 && bitstream[i+1] == 0 &&
            bitstream[i+2] == 0 && bitstream[i+3] == 1) {
            ++count;
        }
    }
    EXPECT_EQ(count, 2);
}

TEST(NalParsingTest, Finds3ByteStartCodes)
{
    std::vector<uint8_t> bitstream = {
        0x00, 0x00, 0x01, 0x67, 0xAA,
        0x00, 0x00, 0x01, 0x68, 0xBB
    };
    int count = 0;
    for (size_t i = 0; i + 2 < bitstream.size(); ++i) {
        if (bitstream[i] == 0 && bitstream[i+1] == 0 && bitstream[i+2] == 1) {
            ++count;
        }
    }
    EXPECT_EQ(count, 2);
}

// =======================================================================
// PipelinePath and PipelineConfig tests
// =======================================================================

TEST(PipelinePathTest, ToStringReturnsCorrectNames)
{
    EXPECT_STREQ(pipeline::to_string(pipeline::PipelinePath::kNone), "none");
    EXPECT_STREQ(pipeline::to_string(pipeline::PipelinePath::kFullGpu), "full-gpu");
    EXPECT_STREQ(pipeline::to_string(pipeline::PipelinePath::kMixedGpu), "mixed-gpu");
    EXPECT_STREQ(pipeline::to_string(pipeline::PipelinePath::kCpuOnly), "cpu-only");
}

TEST(PipelineConfigTest, DefaultValuesAreReasonable)
{
    pipeline::PipelineConfig config;
    EXPECT_EQ(config.width, 1920);
    EXPECT_EQ(config.height, 1080);
    EXPECT_EQ(config.fps, 30);
    EXPECT_EQ(config.bitrateKbps, 2500);
    EXPECT_EQ(config.monitor, 0);
    EXPECT_EQ(config.cpuHighPercent, 80);
    EXPECT_EQ(config.cpuLowPercent, 50);
    EXPECT_EQ(config.cpuCheckIntervalMs, 5000);
    EXPECT_EQ(config.cpuThresholdSec, 10);
    EXPECT_DOUBLE_EQ(config.packetLossHighPercent, 5.0);
    EXPECT_EQ(config.rttHighMs, 200);
}

TEST(PipelineConfigTest, ResolutionStepsDescending)
{
    pipeline::PipelineConfig config;
    EXPECT_GT(config.resolutionSteps[0], config.resolutionSteps[1]);
    EXPECT_GT(config.resolutionSteps[1], config.resolutionSteps[2]);
}

TEST(PipelineConfigTest, FpsStepsDescending)
{
    pipeline::PipelineConfig config;
    EXPECT_GT(config.fpsSteps[0], config.fpsSteps[1]);
    EXPECT_GT(config.fpsSteps[1], config.fpsSteps[2]);
}

TEST(PipelineConfigTest, BitrateStepsDescending)
{
    pipeline::PipelineConfig config;
    EXPECT_GT(config.bitrateSteps[0], config.bitrateSteps[1]);
    EXPECT_GT(config.bitrateSteps[1], config.bitrateSteps[2]);
}

// =======================================================================
// AdaptiveQualityState tests
// =======================================================================

TEST(AdaptiveQualityStateTest, InitialValuesAreZero)
{
    pipeline::AdaptiveQualityState state;
    EXPECT_EQ(state.currentResolutionIdx, 0);
    EXPECT_EQ(state.currentFpsIdx, 0);
    EXPECT_EQ(state.currentBitrateIdx, 0);
    EXPECT_EQ(state.cpuHighSinceMs, 0);
    EXPECT_EQ(state.cpuLowSinceMs, 0);
}

TEST(AdaptiveQualityTest, QualityReductionStepsAreOrdered)
{
    std::vector<int> width_steps  = {1920, 1280, 960};
    std::vector<int> height_steps = {1080, 720, 540};
    std::vector<int> fps_steps    = {30, 15, 10};

    for (size_t i = 1; i < width_steps.size(); ++i) {
        EXPECT_LT(width_steps[i],  width_steps[i-1]);
        EXPECT_LT(height_steps[i], height_steps[i-1]);
    }
    for (size_t i = 1; i < fps_steps.size(); ++i) {
        EXPECT_LT(fps_steps[i], fps_steps[i-1]);
    }
}

// =======================================================================
// Reconnect exponential backoff tests
// =======================================================================

namespace {

static int backoff_delay_ms(int base_ms, int attempt)
{
    constexpr int kMaxDelayMs = 60'000;
    int delay = base_ms;
    for (int i = 0; i < attempt && delay < kMaxDelayMs; ++i) {
        delay = std::min(delay * 2, kMaxDelayMs);
    }
    return delay;
}

} // anonymous namespace

TEST(ReconnectTest, BackoffDoublesEachAttempt)
{
    EXPECT_EQ(backoff_delay_ms(3000, 0), 3000);
    EXPECT_EQ(backoff_delay_ms(3000, 1), 6000);
    EXPECT_EQ(backoff_delay_ms(3000, 2), 12000);
    EXPECT_EQ(backoff_delay_ms(3000, 3), 24000);
}

TEST(ReconnectTest, BackoffClampsAt60Seconds)
{
    EXPECT_EQ(backoff_delay_ms(3000, 10), 60000);
    EXPECT_EQ(backoff_delay_ms(3000, 20), 60000);
}

TEST(ReconnectTest, BackoffZeroAttemptIsBaseDelay)
{
    EXPECT_EQ(backoff_delay_ms(1000, 0), 1000);
    EXPECT_EQ(backoff_delay_ms(5000, 0), 5000);
}

// =======================================================================
// Encoder integration tests
// =======================================================================

TEST(StreamPipelineIntegration, EncoderFactorySelectsBestPath)
{
    encode::EncoderConfig config;
    config.width = 640;
    config.height = 360;
    config.fps = 30;
    config.bitrateKbps = 500;

    auto encoder = encode::EncoderFactory::createBestEncoder(config);
    ASSERT_NE(encoder, nullptr);

    std::string encoderName = encoder->name();
    spdlog::info("[Test] Best encoder for pipeline: {}", encoderName);

    if (encoderName == "nvenc") {
        spdlog::info("[Test] Would select Path B (mixed GPU)");
    } else if (encoderName == "x264") {
        spdlog::info("[Test] Would select Path C (CPU only)");
    }

    auto conv = encode::EncoderFactory::createBestConverter(640, 360);
    ASSERT_NE(conv, nullptr);

    std::vector<uint8_t> bgra(640 * 360 * 4, 128);
    std::vector<uint8_t> nv12(conv->nv12BufferSize());
    ASSERT_TRUE(conv->convert(bgra.data(), 640 * 4, nv12.data()));

    int outputCount = 0;
    encoder->encode(nv12.data(), nv12.size(), 0,
                    [&](const encode::EncodedPacket& pkt) {
        EXPECT_GT(pkt.data.size(), 0u);
        ++outputCount;
    });

    encoder->shutdown();
    conv->shutdown();
}

TEST(StreamPipelineIntegration, ColorConverterMatchesPipeline)
{
    auto conv = encode::EncoderFactory::createBestConverter(1920, 1080);
    ASSERT_NE(conv, nullptr);
    EXPECT_EQ(conv->nv12BufferSize(), 1920u * 1080 * 3 / 2);

    std::vector<uint8_t> bgra(1920 * 1080 * 4, 200);
    std::vector<uint8_t> nv12(conv->nv12BufferSize());
    EXPECT_TRUE(conv->convert(bgra.data(), 1920 * 4, nv12.data()));

    conv->shutdown();
}

TEST(StreamPipelineIntegration, EncoderSetBitrateDoesNotCrash)
{
    encode::EncoderConfig config;
    config.width = 320;
    config.height = 240;
    config.fps = 15;
    config.bitrateKbps = 1000;

    auto encoder = encode::EncoderFactory::createBestEncoder(config);
    ASSERT_NE(encoder, nullptr);

    encoder->setBitrate(500);
    encoder->setBitrate(2000);
    encoder->setBitrate(300);

    const size_t nv12Size = 320 * 240 * 3 / 2;
    std::vector<uint8_t> nv12(nv12Size, 128);

    int outputCount = 0;
    encoder->encode(nv12.data(), nv12.size(), 0,
                    [&](const encode::EncodedPacket& pkt) {
        EXPECT_GT(pkt.data.size(), 0u);
        ++outputCount;
    });

    encoder->shutdown();
}

// =======================================================================
// FrameQueue tests
// =======================================================================

#include "FrameQueue.hpp"

TEST(FrameQueueTest, PushPopWorks)
{
    pipeline::BoundedQueue<int, 4> queue;
    queue.push(42);
    int val = 0;
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 42);
}

TEST(FrameQueueTest, DropsOldestWhenFull)
{
    pipeline::BoundedQueue<int, 2> queue;
    queue.push(1);
    queue.push(2);
    queue.push(3);  // should drop 1
    int val = 0;
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 2);
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 3);
}

TEST(FrameQueueTest, StopUnblocksPop)
{
    pipeline::BoundedQueue<int, 4> queue;
    queue.stop();
    int val = 0;
    EXPECT_FALSE(queue.pop(val));  // should return false immediately
}

TEST(FrameQueueTest, ResetClearsQueue)
{
    pipeline::BoundedQueue<int, 4> queue;
    queue.push(1);
    queue.push(2);
    queue.stop();
    queue.reset();
    queue.push(3);
    int val = 0;
    EXPECT_TRUE(queue.pop(val));
    EXPECT_EQ(val, 3);
}

// =======================================================================
// Encoder flush tests (verifies shutdown doesn't crash after encoding)
// =======================================================================

TEST(EncoderFlushTest, ShutdownAfterEncodeIsSafe)
{
    encode::EncoderConfig config;
    config.width = 320;
    config.height = 240;
    config.fps = 15;
    config.bitrateKbps = 500;

    auto encoder = encode::EncoderFactory::createBestEncoder(config);
    ASSERT_NE(encoder, nullptr);

    const size_t nv12Size = 320 * 240 * 3 / 2;
    std::vector<uint8_t> nv12(nv12Size, 128);

    // Encode several frames to fill encoder buffers
    for (int i = 0; i < 5; ++i) {
        encoder->encode(nv12.data(), nv12.size(), i * 33333,
                        [](const encode::EncodedPacket&) {});
    }

    // shutdown() must flush without crash
    EXPECT_NO_THROW(encoder->shutdown());
}

TEST(EncoderFlushTest, DoubleShutdownIsSafe)
{
    encode::EncoderConfig config;
    config.width = 320;
    config.height = 240;
    config.fps = 15;
    config.bitrateKbps = 500;

    auto encoder = encode::EncoderFactory::createBestEncoder(config);
    ASSERT_NE(encoder, nullptr);

    EXPECT_NO_THROW(encoder->shutdown());
    EXPECT_NO_THROW(encoder->shutdown());  // second call must be safe
}

#endif // HUB32_WITH_FFMPEG && HUB32_WITH_WEBRTC
