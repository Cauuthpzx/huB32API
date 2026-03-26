#ifdef HUB32_WITH_WEBRTC

#include "hub32agent/webrtc/SignalingClient.hpp"
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <regex>

namespace hub32agent::webrtc {

SignalingClient::SignalingClient(const std::string& serverUrl, const std::string& authToken)
    : m_authToken(authToken)
{
    // Parse host:port from URL
    std::regex urlRegex(R"(https?://([^:/]+):?(\d*))");
    std::smatch match;
    std::string host = "127.0.0.1";
    int port = 11081;

    if (std::regex_search(serverUrl, match, urlRegex)) {
        host = match[1].str();
        if (match[2].matched && !match[2].str().empty()) {
            port = std::stoi(match[2].str());
        }
    }

    m_client = std::make_unique<httplib::Client>(host, port);
    m_client->set_connection_timeout(10);
    m_client->set_read_timeout(30);

    spdlog::info("[SignalingClient] connected to {}:{}", host, port);
}

SignalingClient::~SignalingClient() = default;

std::vector<IceServerConfig> SignalingClient::getIceServers()
{
    httplib::Headers headers = {{"Authorization", "Bearer " + m_authToken}};
    auto res = m_client->Get("/api/v1/stream/ice-servers", headers);

    if (!res || res->status != 200) {
        spdlog::warn("[SignalingClient] getIceServers failed: {}",
                     res ? std::to_string(res->status) : "no response");
        return {};
    }

    std::vector<IceServerConfig> servers;
    try {
        auto j = nlohmann::json::parse(res->body);
        for (const auto& srv : j["iceServers"]) {
            IceServerConfig cfg;
            for (const auto& url : srv["urls"]) {
                cfg.urls.push_back(url.get<std::string>());
            }
            if (srv.contains("username"))   cfg.username   = srv["username"].get<std::string>();
            if (srv.contains("credential")) cfg.credential = srv["credential"].get<std::string>();
            servers.push_back(std::move(cfg));
        }
    } catch (const std::exception& ex) {
        spdlog::error("[SignalingClient] failed to parse ice-servers: {}", ex.what());
    }

    spdlog::debug("[SignalingClient] got {} ICE server(s)", servers.size());
    return servers;
}

std::optional<TransportInfo> SignalingClient::createTransport(
    const std::string& locationId, const std::string& direction)
{
    nlohmann::json body;
    body["locationId"] = locationId;
    body["direction"]  = direction;

    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_authToken},
        {"Content-Type", "application/json"}
    };
    auto res = m_client->Post("/api/v1/stream/transport", headers, body.dump(), "application/json");

    if (!res || res->status != 200) {
        spdlog::warn("[SignalingClient] createTransport failed: {}",
                     res ? std::to_string(res->status) : "no response");
        return std::nullopt;
    }

    try {
        auto j = nlohmann::json::parse(res->body);
        TransportInfo info;
        info.id             = j["id"].get<std::string>();
        info.iceParameters  = j.value("iceParameters", nlohmann::json::object());
        info.iceCandidates  = j.value("iceCandidates", nlohmann::json::array());
        info.dtlsParameters = j.value("dtlsParameters", nlohmann::json::object());
        info.sctpParameters = j.value("sctpParameters", nlohmann::json::object());
        spdlog::info("[SignalingClient] transport created: {}", info.id);
        return info;
    } catch (const std::exception& ex) {
        spdlog::error("[SignalingClient] failed to parse transport response: {}", ex.what());
        return std::nullopt;
    }
}

bool SignalingClient::connectTransport(const std::string& transportId,
                                        const nlohmann::json& dtlsParams)
{
    nlohmann::json body;
    body["dtlsParameters"] = dtlsParams;

    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_authToken},
        {"Content-Type", "application/json"}
    };
    std::string path = "/api/v1/stream/transport/" + transportId + "/connect";
    auto res = m_client->Post(path, headers, body.dump(), "application/json");

    if (!res || res->status != 200) {
        spdlog::warn("[SignalingClient] connectTransport failed: {}",
                     res ? std::to_string(res->status) : "no response");
        return false;
    }

    spdlog::info("[SignalingClient] transport connected: {}", transportId);
    return true;
}

std::string SignalingClient::produce(const std::string& transportId,
                                      const std::string& kind,
                                      const nlohmann::json& rtpParams)
{
    nlohmann::json body;
    body["transportId"]   = transportId;
    body["kind"]          = kind;
    body["rtpParameters"] = rtpParams;

    httplib::Headers headers = {
        {"Authorization", "Bearer " + m_authToken},
        {"Content-Type", "application/json"}
    };
    auto res = m_client->Post("/api/v1/stream/produce", headers, body.dump(), "application/json");

    if (!res || res->status != 200) {
        spdlog::warn("[SignalingClient] produce failed: {}",
                     res ? std::to_string(res->status) : "no response");
        return {};
    }

    try {
        auto j = nlohmann::json::parse(res->body);
        auto producerId = j["id"].get<std::string>();
        spdlog::info("[SignalingClient] producer created: {}", producerId);
        return producerId;
    } catch (const std::exception& ex) {
        spdlog::error("[SignalingClient] failed to parse produce response: {}", ex.what());
        return {};
    }
}

void SignalingClient::closeTransport(const std::string& transportId)
{
    httplib::Headers headers = {{"Authorization", "Bearer " + m_authToken}};
    std::string path = "/api/v1/stream/transport/" + transportId;
    auto res = m_client->Delete(path, headers);

    if (!res || (res->status != 200 && res->status != 204)) {
        spdlog::warn("[SignalingClient] closeTransport failed: {}",
                     res ? std::to_string(res->status) : "no response");
    } else {
        spdlog::info("[SignalingClient] transport closed: {}", transportId);
    }
}

} // namespace hub32agent::webrtc

#endif // HUB32_WITH_WEBRTC
