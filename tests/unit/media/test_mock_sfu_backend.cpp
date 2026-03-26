#include <gtest/gtest.h>
#include "media/MockSfuBackend.hpp"

using namespace hub32api;
using namespace hub32api::media;

// ---------------------------------------------------------------------------
// CreateRouter
// ---------------------------------------------------------------------------

TEST(MockSfuBackendTest, CreateRouter_ReturnsUniqueId)
{
    MockSfuBackend backend;

    auto r1 = backend.createRouter("location-1");
    ASSERT_TRUE(r1.is_ok());
    EXPECT_FALSE(r1.value().empty());

    auto r2 = backend.createRouter("location-2");
    ASSERT_TRUE(r2.is_ok());
    EXPECT_FALSE(r2.value().empty());

    EXPECT_NE(r1.value(), r2.value()) << "Two routers must have different IDs";
}

// ---------------------------------------------------------------------------
// CreateWebRtcTransport
// ---------------------------------------------------------------------------

TEST(MockSfuBackendTest, CreateWebRtcTransport_ReturnsValidInfo)
{
    MockSfuBackend backend;

    auto routerResult = backend.createRouter("loc-transport");
    ASSERT_TRUE(routerResult.is_ok());
    const std::string routerId = routerResult.value();

    auto result = backend.createWebRtcTransport(routerId);
    ASSERT_TRUE(result.is_ok());

    const auto& info = result.value();
    EXPECT_FALSE(info.id.empty());
    EXPECT_TRUE(info.iceParameters.contains("usernameFragment"));
    EXPECT_TRUE(info.iceParameters.contains("password"));
    EXPECT_TRUE(info.iceParameters.value("iceLite", false));
    EXPECT_TRUE(info.iceCandidates.is_array());
    EXPECT_FALSE(info.iceCandidates.empty());
    EXPECT_TRUE(info.dtlsParameters.contains("fingerprints"));
    EXPECT_TRUE(info.dtlsParameters.contains("role"));
}

TEST(MockSfuBackendTest, CreateWebRtcTransport_UnknownRouter_ReturnsError)
{
    MockSfuBackend backend;
    auto result = backend.createWebRtcTransport("nonexistent-router-id");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Produce
// ---------------------------------------------------------------------------

TEST(MockSfuBackendTest, Produce_ReturnsProducerInfo)
{
    MockSfuBackend backend;

    auto routerResult = backend.createRouter("loc-produce");
    ASSERT_TRUE(routerResult.is_ok());

    auto transportResult = backend.createWebRtcTransport(routerResult.value());
    ASSERT_TRUE(transportResult.is_ok());
    const std::string transportId = transportResult.value().id;

    nlohmann::json rtpParams = {
        {"codecs", nlohmann::json::array({
            {{"mimeType", "video/H264"}, {"payloadType", 96}, {"clockRate", 90000}}
        })},
        {"encodings", nlohmann::json::array({
            {{"ssrc", 12345678}}
        })}
    };

    auto result = backend.produce(transportId, "video", rtpParams);
    ASSERT_TRUE(result.is_ok());

    const auto& info = result.value();
    EXPECT_FALSE(info.id.empty());
    EXPECT_EQ(info.kind, "video");
    EXPECT_EQ(info.type, "simple");
}

TEST(MockSfuBackendTest, Produce_UnknownTransport_ReturnsError)
{
    MockSfuBackend backend;
    auto result = backend.produce("nonexistent-transport", "video", {});
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Consume
// ---------------------------------------------------------------------------

TEST(MockSfuBackendTest, Consume_ReturnsConsumerInfo)
{
    MockSfuBackend backend;

    auto routerResult = backend.createRouter("loc-consume");
    ASSERT_TRUE(routerResult.is_ok());
    const std::string routerId = routerResult.value();

    auto sendTransport = backend.createWebRtcTransport(routerId);
    ASSERT_TRUE(sendTransport.is_ok());

    auto recvTransport = backend.createWebRtcTransport(routerId);
    ASSERT_TRUE(recvTransport.is_ok());

    // Produce audio
    auto producerResult = backend.produce(
        sendTransport.value().id, "audio",
        nlohmann::json{
            {"codecs", nlohmann::json::array({
                {{"mimeType", "audio/opus"}, {"payloadType", 111}, {"clockRate", 48000}}
            })}
        });
    ASSERT_TRUE(producerResult.is_ok());

    // Consume it
    auto consumerResult = backend.consume(
        recvTransport.value().id,
        producerResult.value().id,
        nlohmann::json{});
    ASSERT_TRUE(consumerResult.is_ok());

    const auto& info = consumerResult.value();
    EXPECT_FALSE(info.id.empty());
    EXPECT_EQ(info.producerId, producerResult.value().id);
    EXPECT_EQ(info.kind, "audio");
    EXPECT_TRUE(info.rtpParameters.contains("codecs"));
}

// ---------------------------------------------------------------------------
// ConnectTransport
// ---------------------------------------------------------------------------

TEST(MockSfuBackendTest, ConnectTransport_Succeeds)
{
    MockSfuBackend backend;

    auto routerResult = backend.createRouter("loc-connect");
    ASSERT_TRUE(routerResult.is_ok());

    auto transportResult = backend.createWebRtcTransport(routerResult.value());
    ASSERT_TRUE(transportResult.is_ok());

    nlohmann::json dtlsParams = {
        {"role", "client"},
        {"fingerprints", nlohmann::json::array({
            {{"algorithm", "sha-256"}, {"value", "AA:BB:CC:DD"}}
        })}
    };

    auto result = backend.connectTransport(transportResult.value().id, dtlsParams);
    EXPECT_TRUE(result.is_ok());
}

TEST(MockSfuBackendTest, ConnectTransport_UnknownTransport_ReturnsError)
{
    MockSfuBackend backend;
    auto result = backend.connectTransport("nonexistent-transport", {});
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// CloseRouter
// ---------------------------------------------------------------------------

TEST(MockSfuBackendTest, CloseRouter_Succeeds)
{
    MockSfuBackend backend;

    auto routerResult = backend.createRouter("loc-close");
    ASSERT_TRUE(routerResult.is_ok());
    const std::string routerId = routerResult.value();

    // After closing, getRouterRtpCapabilities must fail
    backend.closeRouter(routerId);

    auto capResult = backend.getRouterRtpCapabilities(routerId);
    EXPECT_TRUE(capResult.is_err());
    EXPECT_EQ(capResult.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Pause / Resume Producer
// ---------------------------------------------------------------------------

TEST(MockSfuBackendTest, PauseResumeProducer_Succeeds)
{
    MockSfuBackend backend;

    auto routerResult = backend.createRouter("loc-pause");
    ASSERT_TRUE(routerResult.is_ok());

    auto transportResult = backend.createWebRtcTransport(routerResult.value());
    ASSERT_TRUE(transportResult.is_ok());

    auto producerResult = backend.produce(transportResult.value().id, "video", {});
    ASSERT_TRUE(producerResult.is_ok());

    const std::string producerId = producerResult.value().id;

    EXPECT_TRUE(backend.pauseProducer(producerId).is_ok());
    EXPECT_TRUE(backend.resumeProducer(producerId).is_ok());
}

// ---------------------------------------------------------------------------
// GetRouterRtpCapabilities
// ---------------------------------------------------------------------------

TEST(MockSfuBackendTest, GetRouterRtpCapabilities_ReturnsValidJson)
{
    MockSfuBackend backend;

    auto routerResult = backend.createRouter("loc-caps");
    ASSERT_TRUE(routerResult.is_ok());

    auto capsResult = backend.getRouterRtpCapabilities(routerResult.value());
    ASSERT_TRUE(capsResult.is_ok());

    const auto& caps = capsResult.value();
    EXPECT_TRUE(caps.contains("codecs"));
    EXPECT_TRUE(caps["codecs"].is_array());
    EXPECT_GE(caps["codecs"].size(), 2u) << "Should have at least H264 and opus";

    bool hasVideo = false;
    bool hasAudio = false;
    for (const auto& codec : caps["codecs"]) {
        const std::string kind = codec.value("kind", "");
        if (kind == "video") hasVideo = true;
        if (kind == "audio") hasAudio = true;
    }
    EXPECT_TRUE(hasVideo) << "RTP capabilities must include a video codec";
    EXPECT_TRUE(hasAudio) << "RTP capabilities must include an audio codec";
}
