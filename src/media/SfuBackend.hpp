#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

namespace hub32api::media {

struct TransportInfo {
    std::string id;
    nlohmann::json iceParameters;
    nlohmann::json iceCandidates;
    nlohmann::json dtlsParameters;
    nlohmann::json sctpParameters;
};

struct ProducerInfo {
    std::string id;
    std::string kind;  // "video" or "audio"
    std::string type;  // "simple", "simulcast"
};

struct ConsumerInfo {
    std::string id;
    std::string producerId;
    std::string kind;
    nlohmann::json rtpParameters;
};

/// Abstract SFU backend interface.
/// Implementations: MockSfuBackend (dev/test), MediasoupSfuBackend (production).
class HUB32API_EXPORT SfuBackend
{
public:
    virtual ~SfuBackend() = default;

    /// Create a Router for a room. Returns router ID.
    virtual Result<std::string> createRouter(const std::string& roomId) = 0;

    /// Close a Router.
    virtual void closeRouter(const std::string& routerId) = 0;

    /// Get RTP capabilities for a Router.
    virtual Result<nlohmann::json> getRouterRtpCapabilities(const std::string& routerId) = 0;

    /// Create a WebRTC transport on a Router.
    virtual Result<TransportInfo> createWebRtcTransport(const std::string& routerId) = 0;

    /// Connect a transport (DTLS handshake).
    virtual Result<void> connectTransport(const std::string& transportId,
                                           const nlohmann::json& dtlsParameters) = 0;

    /// Create a Producer on a transport.
    virtual Result<ProducerInfo> produce(const std::string& transportId,
                                          const std::string& kind,
                                          const nlohmann::json& rtpParameters) = 0;

    /// Create a Consumer on a transport for a given Producer.
    virtual Result<ConsumerInfo> consume(const std::string& transportId,
                                          const std::string& producerId,
                                          const nlohmann::json& rtpCapabilities) = 0;

    /// Close a transport.
    virtual void closeTransport(const std::string& transportId) = 0;

    /// Pause/resume a Producer.
    virtual Result<void> pauseProducer(const std::string& producerId) = 0;
    virtual Result<void> resumeProducer(const std::string& producerId) = 0;

    /// Request a keyframe from a Consumer.
    virtual Result<void> requestKeyFrame(const std::string& consumerId) = 0;

    /// Set preferred simulcast layers on a Consumer.
    virtual Result<void> setConsumerPreferredLayers(const std::string& consumerId,
                                                     int spatialLayer, int temporalLayer) = 0;

    /// Get backend name (for logging/debugging).
    virtual std::string backendName() const = 0;
};

} // namespace hub32api::media
