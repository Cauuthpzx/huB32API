#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include "SfuBackend.hpp"
#include "hub32api/export.h"

namespace hub32api::media {

/// In-process mock SFU backend for development and testing on Windows.
/// No real media processing — generates realistic-looking UUID IDs and
/// returns hardcoded WebRTC-compatible JSON structures.
class HUB32API_EXPORT MockSfuBackend : public SfuBackend
{
public:
    MockSfuBackend();
    ~MockSfuBackend() override;

    Result<std::string>    createRouter(const std::string& roomId) override;
    void                   closeRouter(const std::string& routerId) override;
    Result<nlohmann::json> getRouterRtpCapabilities(const std::string& routerId) override;

    Result<TransportInfo> createWebRtcTransport(const std::string& routerId) override;
    Result<void>          connectTransport(const std::string& transportId,
                                           const nlohmann::json& dtlsParameters) override;
    void                  closeTransport(const std::string& transportId) override;

    Result<ProducerInfo> produce(const std::string& transportId,
                                  const std::string& kind,
                                  const nlohmann::json& rtpParameters) override;
    Result<void>         pauseProducer(const std::string& producerId) override;
    Result<void>         resumeProducer(const std::string& producerId) override;

    Result<ConsumerInfo> consume(const std::string& transportId,
                                  const std::string& producerId,
                                  const nlohmann::json& rtpCapabilities) override;
    Result<void>         requestKeyFrame(const std::string& consumerId) override;
    Result<void>         setConsumerPreferredLayers(const std::string& consumerId,
                                                    int spatialLayer, int temporalLayer) override;

    std::string backendName() const override;

private:
    struct RouterState {
        std::string roomId;
    };

    struct TransportState {
        std::string routerId;
        bool connected = false;
    };

    struct ProducerState {
        std::string transportId;
        std::string kind;
        bool paused = false;
    };

    struct ConsumerState {
        std::string transportId;
        std::string producerId;
        std::string kind;
    };

    std::unordered_map<std::string, RouterState>    m_routers;
    std::unordered_map<std::string, TransportState> m_transports;
    std::unordered_map<std::string, ProducerState>  m_producers;
    std::unordered_map<std::string, ConsumerState>  m_consumers;
};

} // namespace hub32api::media
