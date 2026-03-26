#ifdef HUB32_WITH_MEDIASOUP

#include "../core/PrecompiledHeader.hpp"
#include "MediasoupSfuBackend.hpp"

// This file provides the SKELETON implementation.
// The actual mediasoup worker integration requires:
// 1. Building libmediasoup-worker as a static library (Meson)
// 2. Including mediasoup headers (worker/include/lib.hpp)
// 3. Implementing FlatBuffers message encoding/decoding
// 4. Spawning worker threads via mediasoup_worker_run()
//
// For now, this skeleton documents the exact API contract so the
// implementation can be completed on a Linux build environment.
// See: third_party/mediasoup-server-ref/controller/ for reference implementation.

namespace hub32api::media {

struct MediasoupSfuBackend::Impl
{
    Config config;
    // TODO: When building on Linux with mediasoup worker:
    // std::vector<std::thread> workerThreads;
    // std::vector<std::unique_ptr<Channel>> channels;
    // std::unordered_map<std::string, RouterInfo> routers;
    // std::unordered_map<std::string, TransportState> transports;
    // std::unordered_map<std::string, ProducerState> producers;
    // std::unordered_map<std::string, ConsumerState> consumers;
    bool initialized = false;
};

MediasoupSfuBackend::MediasoupSfuBackend(const Config& cfg)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->config = cfg;

    // TODO: On Linux, initialize mediasoup workers:
    // 1. Determine numWorkers (if 0, use std::thread::hardware_concurrency())
    // 2. For each worker:
    //    a. Create Channel with ChannelReadFn/ChannelWriteFn callbacks
    //    b. Spawn thread calling mediasoup_worker_run()
    //    c. Wait for worker ready notification

    spdlog::info("[MediasoupSfuBackend] initialized with {} workers (stub — requires Linux build)",
                 cfg.numWorkers);
}

MediasoupSfuBackend::~MediasoupSfuBackend()
{
    // TODO: Stop all workers, join threads
}

Result<std::string> MediasoupSfuBackend::createRouter(const std::string& roomId)
{
    // TODO: Pick least-loaded worker, send WORKER_CREATE_ROUTER FlatBuffers request
    (void)roomId;
    return Result<std::string>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

void MediasoupSfuBackend::closeRouter(const std::string& routerId)
{
    // TODO: Send ROUTER_CLOSE request via FlatBuffers channel
    (void)routerId;
}

Result<nlohmann::json> MediasoupSfuBackend::getRouterRtpCapabilities(const std::string& routerId)
{
    // TODO: Send ROUTER_DUMP request, extract rtpCapabilities from response
    (void)routerId;
    return Result<nlohmann::json>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

Result<TransportInfo> MediasoupSfuBackend::createWebRtcTransport(const std::string& routerId)
{
    // TODO: Send ROUTER_CREATE_WEBRTCTRANSPORT request with:
    //   - listenInfos (IP/port from config)
    //   - enableUdp=true, enableTcp=true, preferUdp=true
    //   - iceConsentTimeout=30
    // Parse response for ICE candidates, ICE parameters, DTLS parameters
    (void)routerId;
    return Result<TransportInfo>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

Result<void> MediasoupSfuBackend::connectTransport(const std::string& transportId,
                                                     const nlohmann::json& dtlsParameters)
{
    // TODO: Send TRANSPORT_CONNECT request with DTLS parameters
    (void)transportId;
    (void)dtlsParameters;
    return Result<void>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

Result<ProducerInfo> MediasoupSfuBackend::produce(const std::string& transportId,
                                                    const std::string& kind,
                                                    const nlohmann::json& rtpParameters)
{
    // TODO: Send TRANSPORT_PRODUCE request with kind + rtpParameters
    // Parse response for producerId
    (void)transportId;
    (void)kind;
    (void)rtpParameters;
    return Result<ProducerInfo>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

Result<ConsumerInfo> MediasoupSfuBackend::consume(const std::string& transportId,
                                                    const std::string& producerId,
                                                    const nlohmann::json& rtpCapabilities)
{
    // TODO: Send TRANSPORT_CONSUME request with producerId + rtpCapabilities
    // Parse response for consumerId + rtpParameters
    (void)transportId;
    (void)producerId;
    (void)rtpCapabilities;
    return Result<ConsumerInfo>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

void MediasoupSfuBackend::closeTransport(const std::string& transportId)
{
    // TODO: Send TRANSPORT_CLOSE notification
    (void)transportId;
}

Result<void> MediasoupSfuBackend::pauseProducer(const std::string& producerId)
{
    (void)producerId;
    return Result<void>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

Result<void> MediasoupSfuBackend::resumeProducer(const std::string& producerId)
{
    (void)producerId;
    return Result<void>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

Result<void> MediasoupSfuBackend::requestKeyFrame(const std::string& consumerId)
{
    (void)consumerId;
    return Result<void>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

Result<void> MediasoupSfuBackend::setConsumerPreferredLayers(const std::string& consumerId,
                                                               int spatialLayer, int temporalLayer)
{
    (void)consumerId;
    (void)spatialLayer;
    (void)temporalLayer;
    return Result<void>::fail(ApiError{ErrorCode::NotImplemented, "mediasoup backend not built"});
}

} // namespace hub32api::media

#endif // HUB32_WITH_MEDIASOUP
