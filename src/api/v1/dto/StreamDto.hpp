#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace hub32api::api::v1::dto {

struct CreateTransportRequest {
    std::string locationId;
    std::string direction; // "send" or "recv"
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CreateTransportRequest, locationId, direction)

struct CreateTransportResponse {
    std::string id;
    nlohmann::json iceParameters;
    nlohmann::json iceCandidates;
    nlohmann::json dtlsParameters;
};

inline void to_json(nlohmann::json& j, const CreateTransportResponse& r) {
    j = {{"id", r.id}, {"iceParameters", r.iceParameters},
         {"iceCandidates", r.iceCandidates}, {"dtlsParameters", r.dtlsParameters}};
}

struct ConnectTransportRequest {
    nlohmann::json dtlsParameters;
};

inline void from_json(const nlohmann::json& j, ConnectTransportRequest& r) {
    r.dtlsParameters = j.value("dtlsParameters", nlohmann::json{});
}

struct ProduceRequest {
    std::string transportId;
    std::string kind; // "video" or "audio"
    nlohmann::json rtpParameters;
};

inline void from_json(const nlohmann::json& j, ProduceRequest& r) {
    r.transportId = j.value("transportId", "");
    r.kind = j.value("kind", "");
    r.rtpParameters = j.value("rtpParameters", nlohmann::json{});
}

struct ProduceResponse {
    std::string id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProduceResponse, id)

struct ConsumeRequest {
    std::string transportId;
    std::string producerId;
    nlohmann::json rtpCapabilities;
};

inline void from_json(const nlohmann::json& j, ConsumeRequest& r) {
    r.transportId = j.value("transportId", "");
    r.producerId = j.value("producerId", "");
    r.rtpCapabilities = j.value("rtpCapabilities", nlohmann::json{});
}

struct ConsumeResponse {
    std::string id;
    std::string producerId;
    std::string kind;
    nlohmann::json rtpParameters;
};

inline void to_json(nlohmann::json& j, const ConsumeResponse& r) {
    j = {{"id", r.id}, {"producerId", r.producerId}, {"kind", r.kind},
         {"rtpParameters", r.rtpParameters}};
}

struct IceServer {
    std::vector<std::string> urls;
    std::string username;
    std::string credential;
};

inline void to_json(nlohmann::json& j, const IceServer& s) {
    j = {{"urls", s.urls}};
    if (!s.username.empty()) j["username"] = s.username;
    if (!s.credential.empty()) j["credential"] = s.credential;
}

} // namespace hub32api::api::v1::dto
