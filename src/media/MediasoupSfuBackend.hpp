#pragma once

// Only available when building with mediasoup support (Linux)
#ifdef HUB32_WITH_MEDIASOUP

#include "SfuBackend.hpp"
#include <memory>
#include <string>

namespace hub32api::media {

/// Production SFU backend using embedded mediasoup C++ worker.
/// Only available on Linux builds with HUB32_WITH_MEDIASOUP defined.
class MediasoupSfuBackend : public SfuBackend
{
public:
    struct Config {
        int numWorkers = 0;        // 0 = auto (CPU cores)
        std::string logLevel = "warn";
        int rtcMinPort = 40000;
        int rtcMaxPort = 49999;
        std::string dtlsCertFile;
        std::string dtlsKeyFile;
    };

    explicit MediasoupSfuBackend(const Config& cfg);
    ~MediasoupSfuBackend() override;

    // SfuBackend interface implementation
    Result<std::string> createRouter(const std::string& roomId) override;
    void closeRouter(const std::string& routerId) override;
    Result<nlohmann::json> getRouterRtpCapabilities(const std::string& routerId) override;
    Result<TransportInfo> createWebRtcTransport(const std::string& routerId) override;
    Result<void> connectTransport(const std::string& transportId,
                                   const nlohmann::json& dtlsParameters) override;
    Result<ProducerInfo> produce(const std::string& transportId,
                                  const std::string& kind,
                                  const nlohmann::json& rtpParameters) override;
    Result<ConsumerInfo> consume(const std::string& transportId,
                                  const std::string& producerId,
                                  const nlohmann::json& rtpCapabilities) override;
    void closeTransport(const std::string& transportId) override;
    Result<void> pauseProducer(const std::string& producerId) override;
    Result<void> resumeProducer(const std::string& producerId) override;
    Result<void> requestKeyFrame(const std::string& consumerId) override;
    Result<void> setConsumerPreferredLayers(const std::string& consumerId,
                                             int spatialLayer, int temporalLayer) override;
    std::string backendName() const override { return "mediasoup"; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hub32api::media

#endif // HUB32_WITH_MEDIASOUP
