#pragma once

#ifdef HUB32_WITH_WEBRTC

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace httplib { class Client; }

namespace hub32agent::webrtc {

/// @brief ICE server configuration from hub32api.
struct IceServerConfig {
    std::vector<std::string> urls;
    std::string username;
    std::string credential;
};

/// @brief Transport creation result from hub32api.
struct TransportInfo {
    std::string id;
    nlohmann::json iceParameters;
    nlohmann::json iceCandidates;
    nlohmann::json dtlsParameters;
    nlohmann::json sctpParameters;
};

/// @brief REST client for WebRTC signaling with hub32api server.
/// Uses the existing httplib::Client for HTTP requests.
class SignalingClient
{
public:
    /// @param serverUrl  Base URL (e.g., "http://10.0.0.1:11081")
    /// @param authToken  JWT Bearer token for authentication.
    SignalingClient(const std::string& serverUrl, const std::string& authToken);
    ~SignalingClient();

    /// @brief GET /api/v1/stream/ice-servers
    std::vector<IceServerConfig> getIceServers();

    /// @brief POST /api/v1/stream/transport
    /// @param locationId  Room/location this transport is for.
    /// @param direction   "send" or "recv".
    /// @return Transport info with ICE/DTLS parameters, or nullopt on failure.
    std::optional<TransportInfo> createTransport(const std::string& locationId,
                                                  const std::string& direction);

    /// @brief POST /api/v1/stream/transport/:id/connect
    /// @param transportId  Transport to connect.
    /// @param dtlsParams   Client DTLS parameters.
    /// @return true on success.
    bool connectTransport(const std::string& transportId,
                          const nlohmann::json& dtlsParams);

    /// @brief POST /api/v1/stream/produce
    /// @param transportId  Transport to produce on.
    /// @param kind         "video" or "audio".
    /// @param rtpParams    RTP parameters.
    /// @return Producer ID, or empty on failure.
    std::string produce(const std::string& transportId,
                         const std::string& kind,
                         const nlohmann::json& rtpParams);

    /// @brief DELETE /api/v1/stream/transport/:id
    void closeTransport(const std::string& transportId);

private:
    std::unique_ptr<httplib::Client> m_client;
    std::string m_authToken;
};

} // namespace hub32agent::webrtc

#endif // HUB32_WITH_WEBRTC
