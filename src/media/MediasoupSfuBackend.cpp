#ifdef HUB32_WITH_MEDIASOUP

#include "../core/PrecompiledHeader.hpp"
#include "MediasoupSfuBackend.hpp"
#include "WorkerChannel.hpp"

#include <thread>
#include <vector>

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

struct MediasoupSfuBackend::Impl
{
    Config config;
    std::vector<std::unique_ptr<WorkerChannel>> channels;
    std::vector<std::thread> workerThreads;
    bool initialized = false;
    int numWorkers = 0;
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
