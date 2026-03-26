#pragma once

#ifdef HUB32_WITH_MEDIASOUP

#include <vector>
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

// NOTE: These FlatBuffers includes require generated headers from:
//   flatc --cpp third_party/mediasoup-server-ref/worker/fbs/*.fbs
// Generated to: third_party/mediasoup-server-ref/worker/fbs/FBS/
#include "FBS/message_generated.h"
#include "FBS/request_generated.h"
#include "FBS/response_generated.h"
#include "FBS/notification_generated.h"
#include "FBS/worker_generated.h"
#include "FBS/router_generated.h"
#include "FBS/webRtcTransport_generated.h"
#include "FBS/transport_generated.h"
#include "FBS/sctpParameters_generated.h"
#include "FBS/consumer_generated.h"
#include "FBS/rtpParameters_generated.h"
#include <flatbuffers/flatbuffers.h>

namespace hub32api::media {

/// @brief Builds FlatBuffers messages for mediasoup worker protocol.
/// Each method returns a size-prefixed FlatBuffers buffer ready to send
/// via WorkerChannel::request() or WorkerChannel::notify().
///
/// Requires Linux build with flatc-generated headers. See docs/LINUX_MEDIASOUP_BUILD.md.
class WorkerMessageBuilder
{
public:
    /// @brief Build WORKER_CREATE_ROUTER request.
    /// @param requestId Unique request ID.
    /// @param routerId  UUID for the new router.
    static std::vector<uint8_t> createRouterRequest(uint32_t requestId,
                                                     const std::string& routerId);

    /// @brief Build ROUTER_CLOSE request.
    /// @param requestId Unique request ID.
    /// @param routerId  UUID of the router to close.
    static std::vector<uint8_t> closeRouterRequest(uint32_t requestId,
                                                    const std::string& routerId);

    /// @brief Build ROUTER_CREATE_WEBRTCTRANSPORT request.
    /// @param requestId   Unique request ID.
    /// @param routerId    Target router UUID.
    /// @param transportId UUID for the new transport.
    /// @param listenIp    IP address to listen on (e.g. "0.0.0.0").
    /// @param rtcMinPort  Minimum RTC port number.
    /// @param rtcMaxPort  Maximum RTC port number.
    static std::vector<uint8_t> createWebRtcTransportRequest(
        uint32_t requestId,
        const std::string& routerId,
        const std::string& transportId,
        const std::string& listenIp,
        int rtcMinPort, int rtcMaxPort);

    /// @brief Build WEBRTCTRANSPORT_CONNECT request.
    /// @param requestId    Unique request ID.
    /// @param transportId  Target transport UUID.
    /// @param dtlsParams   Client DTLS parameters JSON with "fingerprints" and "role".
    static std::vector<uint8_t> connectTransportRequest(
        uint32_t requestId,
        const std::string& transportId,
        const nlohmann::json& dtlsParams);

    /// @brief Build TRANSPORT_CLOSE notification (one-way, no response).
    /// @param transportId UUID of the transport to close.
    static std::vector<uint8_t> closeTransportNotification(const std::string& transportId);

    /// @brief Build WORKER_CLOSE request (shutdown worker).
    /// @param requestId Unique request ID.
    static std::vector<uint8_t> closeWorkerRequest(uint32_t requestId);

    /// @brief Parse a response message. Returns true if accepted.
    /// @param data         Raw FlatBuffers response bytes.
    /// @param expectedId   Expected request ID to match.
    /// @param responseBody On success, receives the raw response bytes for further parsing.
    /// @param errorReason  On failure, receives the error description.
    static bool parseResponse(const std::vector<uint8_t>& data,
                              uint32_t expectedId,
                              std::vector<uint8_t>& responseBody,
                              std::string& errorReason);

    /// @brief Parse WebRtcTransport dump response into JSON.
    /// Extracts: iceParameters, iceCandidates, dtlsParameters, sctpParameters.
    /// @param responseBody Raw response bytes from parseResponse().
    static nlohmann::json parseWebRtcTransportDump(const std::vector<uint8_t>& responseBody);

    /// @brief Build TRANSPORT_PRODUCE request.
    static std::vector<uint8_t> createProduceRequest(
        uint32_t requestId,
        const std::string& transportId,
        const std::string& producerId,
        const std::string& kind,
        const nlohmann::json& rtpParameters);

    /// @brief Build TRANSPORT_CONSUME request.
    static std::vector<uint8_t> createConsumeRequest(
        uint32_t requestId,
        const std::string& transportId,
        const std::string& consumerId,
        const std::string& producerId,
        const nlohmann::json& rtpCapabilities);

    /// @brief Build PRODUCER_PAUSE request.
    static std::vector<uint8_t> createPauseProducerRequest(
        uint32_t requestId,
        const std::string& producerId);

    /// @brief Build PRODUCER_RESUME request.
    static std::vector<uint8_t> createResumeProducerRequest(
        uint32_t requestId,
        const std::string& producerId);

    /// @brief Build CONSUMER_REQUEST_KEY_FRAME request.
    static std::vector<uint8_t> createRequestKeyFrameRequest(
        uint32_t requestId,
        const std::string& consumerId);

    /// @brief Build CONSUMER_SET_PREFERRED_LAYERS request.
    static std::vector<uint8_t> createSetPreferredLayersRequest(
        uint32_t requestId,
        const std::string& consumerId,
        int spatialLayer, int temporalLayer);

    /// @brief Parse TRANSPORT_PRODUCE response to extract producer type.
    static bool parseProduceResponse(const std::vector<uint8_t>& data,
                                      uint32_t expectedId,
                                      std::string& type);

    /// @brief Parse TRANSPORT_CONSUME response to extract consumer details as JSON.
    static nlohmann::json parseConsumeResponse(const std::vector<uint8_t>& responseBody);
};

} // namespace hub32api::media

#endif // HUB32_WITH_MEDIASOUP
