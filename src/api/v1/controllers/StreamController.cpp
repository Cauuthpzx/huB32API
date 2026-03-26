#include "core/PrecompiledHeader.hpp"
#include "StreamController.hpp"
#include "../dto/StreamDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "api/common/HttpErrorUtil.hpp"
#include "media/SfuBackend.hpp"
#include "media/RoomManager.hpp"
#include "core/internal/I18n.hpp"

#include <httplib.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

using hub32api::api::common::sendError;

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

/**
 * @brief Base64-encodes a raw byte buffer.
 * @param data Pointer to the bytes to encode.
 * @param len  Number of bytes.
 * @return The base64-encoded string.
 */
std::string base64Encode(const unsigned char* data, unsigned int len)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (unsigned int i = 0; i < len; i += 3) {
        const unsigned int b0 = data[i];
        const unsigned int b1 = (i + 1 < len) ? data[i + 1] : 0;
        const unsigned int b2 = (i + 2 < len) ? data[i + 2] : 0;
        const unsigned int triple = (b0 << 16) | (b1 << 8) | b2;

        result.push_back(table[(triple >> 18) & 0x3F]);
        result.push_back(table[(triple >> 12) & 0x3F]);
        result.push_back((i + 1 < len) ? table[(triple >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < len) ? table[triple & 0x3F] : '=');
    }
    return result;
}

/**
 * @brief Generates time-limited TURN credentials using HMAC-SHA1.
 *
 * Compatible with coturn REST API (--use-auth-secret):
 *   username = "<unix-timestamp>:hub32user"
 *   credential = base64(HMAC-SHA1(secret, username))
 *
 * @param secret       The shared HMAC secret (matching coturn config).
 * @param ttlSeconds   How long the credential should be valid (default 3600).
 * @return A pair of {username, credential}.
 */
std::pair<std::string, std::string> generateTurnCredentials(
    const std::string& secret, int ttlSeconds = 3600)
{
    const auto expiry = std::chrono::system_clock::now()
        + std::chrono::seconds(ttlSeconds);
    const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        expiry.time_since_epoch()).count();

    const std::string username = std::to_string(timestamp) + ":hub32user";

    unsigned char hmacResult[EVP_MAX_MD_SIZE];
    unsigned int hmacLen = 0;
    HMAC(EVP_sha1(),
         secret.data(), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(username.data()),
         username.size(),
         hmacResult, &hmacLen);

    const std::string credential = base64Encode(hmacResult, hmacLen);
    return {username, credential};
}

} // anonymous namespace

namespace hub32api::api::v1 {

StreamController::StreamController(
    media::RoomManager& roomManager,
    media::SfuBackend& backend,
    const std::string& turnSecret,
    const std::string& turnServerUrl)
    : m_roomManager(roomManager)
    , m_backend(backend)
    , m_turnSecret(turnSecret)
    , m_turnServerUrl(turnServerUrl)
{}

// ── POST /api/v1/stream/transport ────────────────────────────────────────────

void StreamController::handleCreateTransport(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    dto::CreateTransportRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::CreateTransportRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.locationId.empty()) {
        sendError(res, 400, tr(lang, "error.location_id_required"));
        return;
    }
    if (dto.direction != "send" && dto.direction != "recv") {
        sendError(res, 400, tr(lang, "error.direction_required"));
        return;
    }

    // Get or create a Router for this location
    auto routerResult = m_roomManager.getOrCreateRouter(dto.locationId);
    if (routerResult.is_err()) {
        sendError(res, 500, tr(lang, "error.router_creation_failed"),
                  routerResult.error().message);
        return;
    }
    const auto& routerId = routerResult.value();

    // Create WebRTC transport on the Router
    auto transportResult = m_backend.createWebRtcTransport(routerId);
    if (transportResult.is_err()) {
        sendError(res, 500, tr(lang, "error.transport_creation_failed"),
                  transportResult.error().message);
        return;
    }

    const auto& info = transportResult.value();
    dto::CreateTransportResponse resp;
    resp.id             = info.id;
    resp.iceParameters  = info.iceParameters;
    resp.iceCandidates  = info.iceCandidates;
    resp.dtlsParameters = info.dtlsParameters;

    const nlohmann::json j = resp;
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

// ── POST /api/v1/stream/transport/:id/connect ────────────────────────────────

void StreamController::handleConnectTransport(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.transport_id_required"));
        return;
    }
    const std::string transportId = req.matches[1].str();

    dto::ConnectTransportRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::ConnectTransportRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    auto result = m_backend.connectTransport(transportId, dto.dtlsParameters);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.transport_not_found"),
                  result.error().message);
        return;
    }

    nlohmann::json j;
    j["status"] = "connected";
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ── POST /api/v1/stream/produce ──────────────────────────────────────────────

void StreamController::handleProduce(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    dto::ProduceRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::ProduceRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.transportId.empty()) {
        sendError(res, 400, tr(lang, "error.transport_id_required"));
        return;
    }
    if (dto.kind != "video" && dto.kind != "audio") {
        sendError(res, 400, tr(lang, "error.kind_required"));
        return;
    }

    auto result = m_backend.produce(dto.transportId, dto.kind, dto.rtpParameters);
    if (result.is_err()) {
        sendError(res, 500, tr(lang, "error.transport_not_found"),
                  result.error().message);
        return;
    }

    const auto& info = result.value();
    dto::ProduceResponse resp;
    resp.id = info.id;

    const nlohmann::json j = resp;
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

// ── POST /api/v1/stream/consume ──────────────────────────────────────────────

void StreamController::handleConsume(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    dto::ConsumeRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::ConsumeRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.transportId.empty()) {
        sendError(res, 400, tr(lang, "error.transport_id_required"));
        return;
    }
    if (dto.producerId.empty()) {
        sendError(res, 400, tr(lang, "error.producer_id_required"));
        return;
    }

    auto result = m_backend.consume(dto.transportId, dto.producerId, dto.rtpCapabilities);
    if (result.is_err()) {
        sendError(res, 500, tr(lang, "error.transport_not_found"),
                  result.error().message);
        return;
    }

    const auto& info = result.value();
    dto::ConsumeResponse resp;
    resp.id            = info.id;
    resp.producerId    = info.producerId;
    resp.kind          = info.kind;
    resp.rtpParameters = info.rtpParameters;

    const nlohmann::json j = resp;
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

// ── DELETE /api/v1/stream/transport/:id ──────────────────────────────────────

void StreamController::handleCloseTransport(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.transport_id_required"));
        return;
    }
    const std::string transportId = req.matches[1].str();

    m_backend.closeTransport(transportId);
    res.status = 204;
}

// ── GET /api/v1/stream/ice-servers ───────────────────────────────────────────

void StreamController::handleGetIceServers(
    const httplib::Request& /*req*/, httplib::Response& res)
{
    nlohmann::json iceServers = nlohmann::json::array();

    if (m_turnServerUrl.empty()) {
        // No TURN server configured — return only public STUN
        dto::IceServer stun;
        stun.urls = {"stun:stun.l.google.com:19302"};
        iceServers.push_back(nlohmann::json(stun));
    } else {
        // STUN via the TURN server
        dto::IceServer stun;
        stun.urls = {"stun:" + m_turnServerUrl + ":3478"};
        iceServers.push_back(nlohmann::json(stun));

        if (!m_turnSecret.empty()) {
            // Generate time-limited HMAC credentials (coturn REST API)
            auto [username, credential] = generateTurnCredentials(m_turnSecret);

            dto::IceServer turn;
            turn.urls       = {"turn:" + m_turnServerUrl + ":443?transport=tcp"};
            turn.username   = username;
            turn.credential = credential;
            iceServers.push_back(nlohmann::json(turn));
        }
    }

    nlohmann::json j;
    j["iceServers"] = iceServers;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ── GET /api/v1/stream/capabilities/:locationId ─────────────────────────────

void StreamController::handleGetCapabilities(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.location_id_required"));
        return;
    }
    const std::string locationId = req.matches[1].str();

    auto result = m_roomManager.getRtpCapabilities(locationId);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.router_creation_failed"),
                  result.error().message);
        return;
    }

    nlohmann::json j;
    j["rtpCapabilities"] = result.value();
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

} // namespace hub32api::api::v1
