#ifdef HUB32_WITH_MEDIASOUP

#include "../core/PrecompiledHeader.hpp"
#include "MediasoupSfuBackend.hpp"
#include "WorkerChannel.hpp"
#include "WorkerMessageBuilder.hpp"
#include "../core/internal/CryptoUtils.hpp"

#include <thread>
#include <vector>
#include <mutex>
#include <unordered_map>

// mediasoup worker C API (linked from libmediasoup-worker.a)
extern "C" {
    int mediasoup_worker_run(
        int argc, char* argv[],
        const char* version,
        int consumerChannelFd, int producerChannelFd,
        void* channelReadFn, void* channelReadCtx,
        void* channelWriteFn, void* channelWriteCtx);
}

namespace hub32api::media {

using hub32api::core::internal::CryptoUtils;

namespace {

/// @brief Build hardcoded RTP capabilities (H264 + Opus).
/// These match the server-side codecs we support. The real mediasoup worker
/// returns capabilities from ROUTER_DUMP, but since we define the codecs
/// at router creation time, this is equivalent.
nlohmann::json makeRtpCapabilities()
{
    return nlohmann::json{
        {"codecs", nlohmann::json::array({
            {
                {"mimeType",             "video/H264"},
                {"kind",                 "video"},
                {"clockRate",            90000},
                {"preferredPayloadType", 96},
                {"parameters", {
                    {"level-asymmetry-allowed", 1},
                    {"packetization-mode",      1},
                    {"profile-level-id",        "42e01f"}
                }},
                {"rtcpFeedback", nlohmann::json::array({
                    {{"type", "nack"}},
                    {{"type", "nack"}, {"parameter", "pli"}},
                    {{"type", "ccm"},  {"parameter", "fir"}},
                    {{"type", "goog-remb"}}
                })}
            },
            {
                {"mimeType",             "audio/opus"},
                {"kind",                 "audio"},
                {"clockRate",            48000},
                {"channels",             2},
                {"preferredPayloadType", 111},
                {"parameters", {
                    {"useinbandfec", 1}
                }}
            }
        })}
    };
}

} // anonymous namespace

struct MediasoupSfuBackend::Impl
{
    Config config;
    std::vector<std::unique_ptr<WorkerChannel>> channels;
    std::vector<std::thread> workerThreads;
    bool initialized = false;
    int numWorkers = 0;

    // Entity tracking
    std::mutex entityMutex;
    std::unordered_map<std::string, int> routerToWorker;             // routerId -> worker index
    std::unordered_map<std::string, std::string> transportToRouter;  // transportId -> routerId
    std::unordered_map<std::string, std::string> producerToTransport; // producerId -> transportId
    std::unordered_map<std::string, std::string> consumerToTransport; // consumerId -> transportId
    int nextWorkerIdx = 0;

    /// @brief Pick the next worker using round-robin scheduling.
    int pickWorker()
    {
        if (numWorkers == 0) return -1;
        int idx = nextWorkerIdx % numWorkers;
        nextWorkerIdx++;
        return idx;
    }

    /// @brief Get the WorkerChannel for a given router.
    /// @return Pointer to the channel, or nullptr if routerId not found.
    WorkerChannel* channelForRouter(const std::string& routerId)
    {
        auto it = routerToWorker.find(routerId);
        if (it == routerToWorker.end()) return nullptr;
        return channels[it->second].get();
    }

    /// @brief Get the WorkerChannel for a given transport (via its router).
    /// @return Pointer to the channel, or nullptr if transportId not found.
    WorkerChannel* channelForTransport(const std::string& transportId)
    {
        auto tIt = transportToRouter.find(transportId);
        if (tIt == transportToRouter.end()) return nullptr;
        return channelForRouter(tIt->second);
    }

    /// @brief Get the WorkerChannel for a given producer (via its transport).
    /// @return Pointer to the channel, or nullptr if producerId not found.
    WorkerChannel* channelForProducer(const std::string& producerId)
    {
        auto it = producerToTransport.find(producerId);
        if (it == producerToTransport.end()) return nullptr;
        return channelForTransport(it->second);
    }

    /// @brief Get the WorkerChannel for a given consumer (via its transport).
    /// @return Pointer to the channel, or nullptr if consumerId not found.
    WorkerChannel* channelForConsumer(const std::string& consumerId)
    {
        auto it = consumerToTransport.find(consumerId);
        if (it == consumerToTransport.end()) return nullptr;
        return channelForTransport(it->second);
    }
};

MediasoupSfuBackend::MediasoupSfuBackend(const Config& cfg)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->config = cfg;

    // Determine worker count: 0 means auto-detect from CPU cores
    m_impl->numWorkers = cfg.numWorkers;
    if (m_impl->numWorkers <= 0) {
        m_impl->numWorkers = static_cast<int>(std::thread::hardware_concurrency());
        if (m_impl->numWorkers <= 0) {
            m_impl->numWorkers = 1;
        }
    }

    spdlog::info("[MediasoupSfuBackend] spawning {} workers (rtcMinPort={}, rtcMaxPort={})",
                 m_impl->numWorkers, cfg.rtcMinPort, cfg.rtcMaxPort);

    // Spawn one worker thread per configured worker
    for (int i = 0; i < m_impl->numWorkers; ++i) {
        auto channel = std::make_unique<WorkerChannel>();
        auto* channelPtr = channel.get();

        // Build argv for mediasoup_worker_run()
        std::string logLevel = "--logLevel=" + cfg.logLevel;
        std::string rtcMin = "--rtcMinPort=" + std::to_string(cfg.rtcMinPort);
        std::string rtcMax = "--rtcMaxPort=" + std::to_string(cfg.rtcMaxPort);

        std::vector<std::string> argStrings = {
            "mediasoup-worker", logLevel, rtcMin, rtcMax
        };
        if (!cfg.dtlsCertFile.empty()) {
            argStrings.push_back("--dtlsCertificateFile=" + cfg.dtlsCertFile);
        }
        if (!cfg.dtlsKeyFile.empty()) {
            argStrings.push_back("--dtlsPrivateKeyFile=" + cfg.dtlsKeyFile);
        }

        // Shared ownership keeps strings alive for the worker thread's lifetime
        auto argsCopy = std::make_shared<std::vector<std::string>>(std::move(argStrings));
        auto argv = std::make_shared<std::vector<char*>>();
        for (auto& s : *argsCopy) {
            argv->push_back(s.data());
        }

        std::thread worker([channelPtr, argsCopy, argv, i]() {
            spdlog::info("[MediasoupSfuBackend] worker {} starting", i);

            int rc = mediasoup_worker_run(
                static_cast<int>(argv->size()),
                argv->data(),
                "3.14.7",
                0, 0,  // no pipe fds — using in-process channel callbacks
                reinterpret_cast<void*>(&WorkerChannel::channelRead),
                channelPtr,
                reinterpret_cast<void*>(&WorkerChannel::channelWrite),
                channelPtr
            );

            spdlog::info("[MediasoupSfuBackend] worker {} exited with code {}", i, rc);
        });

        m_impl->channels.push_back(std::move(channel));
        m_impl->workerThreads.push_back(std::move(worker));
    }

    m_impl->initialized = true;
    spdlog::info("[MediasoupSfuBackend] {} workers spawned", m_impl->numWorkers);
}

MediasoupSfuBackend::~MediasoupSfuBackend()
{
    spdlog::info("[MediasoupSfuBackend] shutting down {} workers", m_impl->numWorkers);

    // Close all channels — signals workers to exit their event loops
    for (auto& ch : m_impl->channels) {
        ch->close();
    }

    // Join all worker threads
    for (auto& t : m_impl->workerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }

    spdlog::info("[MediasoupSfuBackend] all workers stopped");
}

Result<std::string> MediasoupSfuBackend::createRouter(const std::string& roomId)
{
    std::lock_guard lock(m_impl->entityMutex);
    int workerIdx = m_impl->pickWorker();
    if (workerIdx < 0) {
        return Result<std::string>::fail(
            ApiError{ErrorCode::ServiceUnavailable, "no workers available"});
    }

    auto* channel = m_impl->channels[workerIdx].get();
    auto reqId = channel->genRequestId();

    // Generate a unique router UUID
    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        return Result<std::string>::fail(uuidResult.error());
    }
    std::string routerId = uuidResult.take();

    auto reqData = WorkerMessageBuilder::createRouterRequest(reqId, routerId);
    auto respData = channel->request(reqId, reqData);

    std::vector<uint8_t> body;
    std::string errorReason;
    if (!WorkerMessageBuilder::parseResponse(respData, reqId, body, errorReason)) {
        return Result<std::string>::fail(ApiError{ErrorCode::InternalError,
            "[MediasoupSfuBackend] createRouter failed: " + errorReason});
    }

    m_impl->routerToWorker[routerId] = workerIdx;
    spdlog::info("[MediasoupSfuBackend] created router {} for room {} on worker {}",
                 routerId, roomId, workerIdx);
    return Result<std::string>::ok(std::move(routerId));
}

void MediasoupSfuBackend::closeRouter(const std::string& routerId)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForRouter(routerId);
    if (!channel) {
        spdlog::warn("[MediasoupSfuBackend] closeRouter: unknown routerId={}", routerId);
        return;
    }

    auto reqId = channel->genRequestId();
    auto reqData = WorkerMessageBuilder::closeRouterRequest(reqId, routerId);
    channel->request(reqId, reqData);

    // Collect transports belonging to this router
    std::vector<std::string> transportIds;
    for (const auto& [tid, rid] : m_impl->transportToRouter) {
        if (rid == routerId) transportIds.push_back(tid);
    }

    // Remove producers/consumers belonging to those transports
    for (const auto& tid : transportIds) {
        for (auto it = m_impl->producerToTransport.begin(); it != m_impl->producerToTransport.end();) {
            if (it->second == tid) { it = m_impl->producerToTransport.erase(it); } else { ++it; }
        }
        for (auto it = m_impl->consumerToTransport.begin(); it != m_impl->consumerToTransport.end();) {
            if (it->second == tid) { it = m_impl->consumerToTransport.erase(it); } else { ++it; }
        }
        m_impl->transportToRouter.erase(tid);
    }
    m_impl->routerToWorker.erase(routerId);
    spdlog::info("[MediasoupSfuBackend] closed router {}", routerId);
}

Result<nlohmann::json> MediasoupSfuBackend::getRouterRtpCapabilities(const std::string& routerId)
{
    std::lock_guard lock(m_impl->entityMutex);
    if (m_impl->routerToWorker.find(routerId) == m_impl->routerToWorker.end()) {
        return Result<nlohmann::json>::fail(
            ApiError{ErrorCode::NotFound, "Router not found: " + routerId});
    }

    // Return server-defined RTP capabilities (H264 + Opus).
    // These are the codecs we configure at router creation time.
    // A full implementation could send ROUTER_DUMP and parse capabilities
    // from the response, but the capabilities are deterministic.
    spdlog::debug("[MediasoupSfuBackend] getRouterRtpCapabilities: routerId={}", routerId);
    return Result<nlohmann::json>::ok(makeRtpCapabilities());
}

Result<TransportInfo> MediasoupSfuBackend::createWebRtcTransport(const std::string& routerId)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForRouter(routerId);
    if (!channel) {
        return Result<TransportInfo>::fail(
            ApiError{ErrorCode::NotFound, "Router not found: " + routerId});
    }

    auto reqId = channel->genRequestId();

    // Generate a unique transport UUID
    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        return Result<TransportInfo>::fail(uuidResult.error());
    }
    std::string transportId = uuidResult.take();

    // Use "0.0.0.0" as listen IP (all interfaces). Config provides port range.
    auto reqData = WorkerMessageBuilder::createWebRtcTransportRequest(
        reqId, routerId, transportId,
        "0.0.0.0",
        m_impl->config.rtcMinPort,
        m_impl->config.rtcMaxPort);

    auto respData = channel->request(reqId, reqData);

    std::vector<uint8_t> body;
    std::string errorReason;
    if (!WorkerMessageBuilder::parseResponse(respData, reqId, body, errorReason)) {
        return Result<TransportInfo>::fail(ApiError{ErrorCode::InternalError,
            "[MediasoupSfuBackend] createWebRtcTransport failed: " + errorReason});
    }

    // Parse the transport dump from the response body
    auto dumpJson = WorkerMessageBuilder::parseWebRtcTransportDump(body);

    TransportInfo info;
    info.id = transportId;
    info.iceParameters = dumpJson.value("iceParameters", nlohmann::json::object());
    info.iceCandidates = dumpJson.value("iceCandidates", nlohmann::json::array());
    info.dtlsParameters = dumpJson.value("dtlsParameters", nlohmann::json::object());
    info.sctpParameters = nullptr;  // SCTP not enabled

    m_impl->transportToRouter[transportId] = routerId;
    spdlog::info("[MediasoupSfuBackend] created WebRTC transport {} on router {}",
                 transportId, routerId);
    return Result<TransportInfo>::ok(std::move(info));
}

Result<void> MediasoupSfuBackend::connectTransport(const std::string& transportId,
                                                     const nlohmann::json& dtlsParameters)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForTransport(transportId);
    if (!channel) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Transport not found: " + transportId});
    }

    auto reqId = channel->genRequestId();
    auto reqData = WorkerMessageBuilder::connectTransportRequest(reqId, transportId, dtlsParameters);
    auto respData = channel->request(reqId, reqData);

    std::vector<uint8_t> body;
    std::string errorReason;
    if (!WorkerMessageBuilder::parseResponse(respData, reqId, body, errorReason)) {
        return Result<void>::fail(ApiError{ErrorCode::InternalError,
            "[MediasoupSfuBackend] connectTransport failed: " + errorReason});
    }

    spdlog::info("[MediasoupSfuBackend] connected transport {} (dtlsRole={})",
                 transportId, dtlsParameters.value("role", "<auto>"));
    return Result<void>::ok();
}

Result<ProducerInfo> MediasoupSfuBackend::produce(const std::string& transportId,
                                                    const std::string& kind,
                                                    const nlohmann::json& rtpParameters)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForTransport(transportId);
    if (!channel) {
        return Result<ProducerInfo>::fail(
            ApiError{ErrorCode::NotFound, "Transport not found: " + transportId});
    }

    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) return Result<ProducerInfo>::fail(uuidResult.error());
    std::string producerId = uuidResult.take();

    auto reqId = channel->genRequestId();
    auto reqData = WorkerMessageBuilder::createProduceRequest(
        reqId, transportId, producerId, kind, rtpParameters);
    auto respData = channel->request(reqId, reqData);

    std::vector<uint8_t> body;
    std::string errorReason;
    if (!WorkerMessageBuilder::parseResponse(respData, reqId, body, errorReason)) {
        return Result<ProducerInfo>::fail(ApiError{ErrorCode::InternalError,
            "[MediasoupSfuBackend] produce failed: " + errorReason});
    }

    // Parse producer type from response
    std::string producerType = "simple";
    WorkerMessageBuilder::parseProduceResponse(body, reqId, producerType);

    m_impl->producerToTransport[producerId] = transportId;

    ProducerInfo info;
    info.id = producerId;
    info.kind = kind;
    info.type = producerType;
    spdlog::info("[MediasoupSfuBackend] produced {} ({}) on transport {}", producerId, kind, transportId);
    return Result<ProducerInfo>::ok(std::move(info));
}

Result<ConsumerInfo> MediasoupSfuBackend::consume(const std::string& transportId,
                                                    const std::string& producerId,
                                                    const nlohmann::json& rtpCapabilities)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForTransport(transportId);
    if (!channel) {
        return Result<ConsumerInfo>::fail(
            ApiError{ErrorCode::NotFound, "Transport not found: " + transportId});
    }

    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) return Result<ConsumerInfo>::fail(uuidResult.error());
    std::string consumerId = uuidResult.take();

    auto reqId = channel->genRequestId();
    auto reqData = WorkerMessageBuilder::createConsumeRequest(
        reqId, transportId, consumerId, producerId, rtpCapabilities);
    auto respData = channel->request(reqId, reqData);

    std::vector<uint8_t> body;
    std::string errorReason;
    if (!WorkerMessageBuilder::parseResponse(respData, reqId, body, errorReason)) {
        return Result<ConsumerInfo>::fail(ApiError{ErrorCode::InternalError,
            "[MediasoupSfuBackend] consume failed: " + errorReason});
    }

    // Parse consume response for additional details
    auto consumeDetails = WorkerMessageBuilder::parseConsumeResponse(body);

    m_impl->consumerToTransport[consumerId] = transportId;

    ConsumerInfo info;
    info.id = consumerId;
    info.producerId = producerId;
    info.kind = "video";  // TODO: determine from producer's kind
    info.rtpParameters = rtpCapabilities;  // TODO: parse actual negotiated params
    spdlog::info("[MediasoupSfuBackend] consumed {} from producer {} on transport {}",
                 consumerId, producerId, transportId);
    return Result<ConsumerInfo>::ok(std::move(info));
}

void MediasoupSfuBackend::closeTransport(const std::string& transportId)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForTransport(transportId);
    if (!channel) {
        spdlog::warn("[MediasoupSfuBackend] closeTransport: unknown transportId={}", transportId);
        return;
    }

    // Send one-way notification (no response expected)
    auto notifData = WorkerMessageBuilder::closeTransportNotification(transportId);
    channel->notify(notifData);

    // Remove producers/consumers belonging to this transport
    for (auto it = m_impl->producerToTransport.begin(); it != m_impl->producerToTransport.end();) {
        if (it->second == transportId) { it = m_impl->producerToTransport.erase(it); } else { ++it; }
    }
    for (auto it = m_impl->consumerToTransport.begin(); it != m_impl->consumerToTransport.end();) {
        if (it->second == transportId) { it = m_impl->consumerToTransport.erase(it); } else { ++it; }
    }
    m_impl->transportToRouter.erase(transportId);
    spdlog::info("[MediasoupSfuBackend] closed transport {}", transportId);
}

Result<void> MediasoupSfuBackend::pauseProducer(const std::string& producerId)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForProducer(producerId);
    if (!channel) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Producer not found: " + producerId});
    }

    auto reqId = channel->genRequestId();
    auto reqData = WorkerMessageBuilder::createPauseProducerRequest(reqId, producerId);
    auto respData = channel->request(reqId, reqData);

    std::vector<uint8_t> body;
    std::string errorReason;
    if (!WorkerMessageBuilder::parseResponse(respData, reqId, body, errorReason)) {
        return Result<void>::fail(ApiError{ErrorCode::InternalError,
            "[MediasoupSfuBackend] pauseProducer failed: " + errorReason});
    }

    spdlog::info("[MediasoupSfuBackend] paused producer {}", producerId);
    return Result<void>::ok();
}

Result<void> MediasoupSfuBackend::resumeProducer(const std::string& producerId)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForProducer(producerId);
    if (!channel) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Producer not found: " + producerId});
    }

    auto reqId = channel->genRequestId();
    auto reqData = WorkerMessageBuilder::createResumeProducerRequest(reqId, producerId);
    auto respData = channel->request(reqId, reqData);

    std::vector<uint8_t> body;
    std::string errorReason;
    if (!WorkerMessageBuilder::parseResponse(respData, reqId, body, errorReason)) {
        return Result<void>::fail(ApiError{ErrorCode::InternalError,
            "[MediasoupSfuBackend] resumeProducer failed: " + errorReason});
    }

    spdlog::info("[MediasoupSfuBackend] resumed producer {}", producerId);
    return Result<void>::ok();
}

Result<void> MediasoupSfuBackend::requestKeyFrame(const std::string& consumerId)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForConsumer(consumerId);
    if (!channel) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Consumer not found: " + consumerId});
    }

    auto reqId = channel->genRequestId();
    auto reqData = WorkerMessageBuilder::createRequestKeyFrameRequest(reqId, consumerId);
    auto respData = channel->request(reqId, reqData);

    std::vector<uint8_t> body;
    std::string errorReason;
    if (!WorkerMessageBuilder::parseResponse(respData, reqId, body, errorReason)) {
        return Result<void>::fail(ApiError{ErrorCode::InternalError,
            "[MediasoupSfuBackend] requestKeyFrame failed: " + errorReason});
    }

    spdlog::info("[MediasoupSfuBackend] requested key frame for consumer {}", consumerId);
    return Result<void>::ok();
}

Result<void> MediasoupSfuBackend::setConsumerPreferredLayers(const std::string& consumerId,
                                                               int spatialLayer, int temporalLayer)
{
    std::lock_guard lock(m_impl->entityMutex);
    auto* channel = m_impl->channelForConsumer(consumerId);
    if (!channel) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Consumer not found: " + consumerId});
    }

    auto reqId = channel->genRequestId();
    auto reqData = WorkerMessageBuilder::createSetPreferredLayersRequest(
        reqId, consumerId, spatialLayer, temporalLayer);
    auto respData = channel->request(reqId, reqData);

    std::vector<uint8_t> body;
    std::string errorReason;
    if (!WorkerMessageBuilder::parseResponse(respData, reqId, body, errorReason)) {
        return Result<void>::fail(ApiError{ErrorCode::InternalError,
            "[MediasoupSfuBackend] setConsumerPreferredLayers failed: " + errorReason});
    }

    spdlog::info("[MediasoupSfuBackend] set preferred layers ({},{}) for consumer {}",
                 spatialLayer, temporalLayer, consumerId);
    return Result<void>::ok();
}

} // namespace hub32api::media

#endif // HUB32_WITH_MEDIASOUP
