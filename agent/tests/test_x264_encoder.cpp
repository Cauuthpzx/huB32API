#ifdef HUB32_WITH_FFMPEG

#include <gtest/gtest.h>
#include "hub32agent/encode/EncoderFactory.hpp"
#include "CpuColorConverter.hpp"

using namespace hub32agent::encode;

TEST(CpuColorConverterTest, ConvertBGRAtoNV12)
{
    CpuColorConverter conv;
    ASSERT_TRUE(conv.initialize(640, 360));
    EXPECT_EQ(conv.nv12BufferSize(), 640u * 360 * 3 / 2);

    // Create a dummy BGRA frame (solid blue)
    std::vector<uint8_t> bgra(640 * 360 * 4, 0);
    for (size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i + 0] = 255;  // B
        bgra[i + 1] = 0;    // G
        bgra[i + 2] = 0;    // R
        bgra[i + 3] = 255;  // A
    }

    std::vector<uint8_t> nv12(conv.nv12BufferSize());
    EXPECT_TRUE(conv.convert(bgra.data(), 640 * 4, nv12.data()));

    // Y values should be non-zero for blue
    EXPECT_GT(nv12[0], 0u);

    conv.shutdown();
}

TEST(CpuColorConverterTest, RejectsOddDimensions)
{
    CpuColorConverter conv;
    EXPECT_FALSE(conv.initialize(641, 360));
    EXPECT_FALSE(conv.initialize(640, 361));
    EXPECT_FALSE(conv.initialize(0, 0));
    EXPECT_FALSE(conv.initialize(-1, 360));
}

TEST(X264EncoderTest, EncodeProducesOutput)
{
    EncoderConfig config;
    config.width = 640;
    config.height = 360;
    config.fps = 30;
    config.bitrateKbps = 500;

    auto encoder = EncoderFactory::createEncoder("x264", config);
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->name(), "x264");

    // Create dummy NV12 frame
    const size_t nv12Size = 640 * 360 * 3 / 2;
    std::vector<uint8_t> nv12(nv12Size, 128);  // gray frame

    // Encode 10 frames
    int packetCount = 0;
    for (int i = 0; i < 10; ++i) {
        encoder->encode(nv12.data(), nv12.size(), i * 33333,
                        [&](const EncodedPacket& pkt) {
            EXPECT_GT(pkt.data.size(), 0u);
            packetCount++;
            if (i == 0) {
                // First frame should be a keyframe
                EXPECT_TRUE(pkt.isKeyFrame);
            }
        });
    }

    EXPECT_GT(packetCount, 0);

    encoder->shutdown();
}

TEST(X264EncoderTest, RequestKeyFrame)
{
    EncoderConfig config;
    config.width = 320;
    config.height = 240;
    config.fps = 15;
    config.bitrateKbps = 300;
    config.keyFrameIntervalSec = 10;  // Long interval so we can test forced keyframe

    auto encoder = EncoderFactory::createEncoder("x264", config);
    ASSERT_NE(encoder, nullptr);

    const size_t nv12Size = 320 * 240 * 3 / 2;
    std::vector<uint8_t> nv12(nv12Size, 100);

    // Encode a few normal frames
    for (int i = 0; i < 5; ++i) {
        encoder->encode(nv12.data(), nv12.size(), i * 66666,
                        [](const EncodedPacket&) {});
    }

    // Request keyframe, then encode
    encoder->requestKeyFrame();
    bool gotKeyFrame = false;
    encoder->encode(nv12.data(), nv12.size(), 5 * 66666,
                    [&](const EncodedPacket& pkt) {
        if (pkt.isKeyFrame) gotKeyFrame = true;
    });
    EXPECT_TRUE(gotKeyFrame);

    encoder->shutdown();
}

TEST(EncoderFactoryTest, CreateBestEncoder)
{
    EncoderConfig config;
    config.width = 640;
    config.height = 360;
    config.fps = 30;
    config.bitrateKbps = 500;

    auto encoder = EncoderFactory::createBestEncoder(config);
    ASSERT_NE(encoder, nullptr);
    // Should be x264 on systems without NVENC/QSV
    EXPECT_EQ(encoder->name(), "x264");
    encoder->shutdown();
}

TEST(EncoderFactoryTest, CreateBestConverter)
{
    auto conv = EncoderFactory::createBestConverter(640, 360);
    ASSERT_NE(conv, nullptr);
    EXPECT_EQ(conv->name(), "cpu-manual");
    conv->shutdown();
}

#endif // HUB32_WITH_FFMPEG
