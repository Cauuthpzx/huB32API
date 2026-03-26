#include "../core/PrecompiledHeader.hpp"
#include "MockSfuBackend.hpp"
#include "../core/internal/CryptoUtils.hpp"
#include <sstream>
#include <iomanip>

namespace hub32api::media {

using hub32api::core::internal::CryptoUtils;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

/// Generate a lowercase hex string of `byteCount` random bytes.
std::string randomHex(size_t byteCount)
{
    auto bytesResult = CryptoUtils::randomBytes(byteCount);
    if (bytesResult.is_err()) {
        spdlog::error("[MockSfuBackend] randomBytes failed: {}", bytesResult.error().message);
        return std::string(byteCount * 2, '0');  // fallback
    }
    auto bytes = bytesResult.take();
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : bytes) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

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

TransportInfo makeFakeTransport(const std::string& id)
{
    TransportInfo info;
    info.id = id;
    info.iceParameters = {
        {"usernameFragment", randomHex(8)},
        {"password",         randomHex(24)},
        {"iceLite",          true}
    };
    info.iceCandidates = nlohmann::json::array({
        {
            {"foundation", "udpcandidate"},
            {"priority",   1076302079},
            {"ip",         "0.0.0.0"},
            {"protocol",   "udp"},
            {"port",       40000},
            {"type",       "host"}
        }
    });
    info.dtlsParameters = {
        {"fingerprints", nlohmann::json::array({
            {
                {"algorithm", "sha-256"},
                {"value",     "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:"
                               "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00"}
            }
        })},
        {"role", "auto"}
    };
    info.sctpParameters = nullptr;
    return info;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// MockSfuBackend
// ---------------------------------------------------------------------------

MockSfuBackend::MockSfuBackend()
{
    spdlog::info("[MockSfuBackend] initialized (mock/test mode — no real media)");
}

MockSfuBackend::~MockSfuBackend() = default;

std::string MockSfuBackend::backendName() const
{
    return "MockSfuBackend";
}

// ---------------------------------------------------------------------------
// Router
// ---------------------------------------------------------------------------

Result<std::string> MockSfuBackend::createRouter(const std::string& roomId)
{
    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        spdlog::error("[MockSfuBackend] createRouter failed: {}", uuidResult.error().message);
        return Result<std::string>::fail(
            ApiError{ErrorCode::InternalError, "createRouter failed: " + uuidResult.error().message});
    }
    const std::string routerId = uuidResult.take();
    m_routers[routerId] = RouterState{ roomId };
    spdlog::debug("[MockSfuBackend] createRouter: roomId={} routerId={}", roomId, routerId);
    return Result<std::string>::ok(routerId);
}

void MockSfuBackend::closeRouter(const std::string& routerId)
{
    m_routers.erase(routerId);
    spdlog::debug("[MockSfuBackend] closeRouter: routerId={}", routerId);
}

Result<nlohmann::json> MockSfuBackend::getRouterRtpCapabilities(const std::string& routerId)
{
    if (m_routers.find(routerId) == m_routers.end()) {
        return Result<nlohmann::json>::fail(
            ApiError{ErrorCode::NotFound, "Router not found: " + routerId});
    }
    spdlog::debug("[MockSfuBackend] getRouterRtpCapabilities: routerId={}", routerId);
    return Result<nlohmann::json>::ok(makeRtpCapabilities());
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

Result<TransportInfo> MockSfuBackend::createWebRtcTransport(const std::string& routerId)
{
    if (m_routers.find(routerId) == m_routers.end()) {
        return Result<TransportInfo>::fail(
            ApiError{ErrorCode::NotFound, "Router not found: " + routerId});
    }
    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        spdlog::error("[MockSfuBackend] createWebRtcTransport failed: {}", uuidResult.error().message);
        return Result<TransportInfo>::fail(
            ApiError{ErrorCode::InternalError,
                     "createWebRtcTransport failed: " + uuidResult.error().message});
    }
    const std::string transportId = uuidResult.take();
    m_transports[transportId] = TransportState{ routerId, false };
    TransportInfo info = makeFakeTransport(transportId);
    spdlog::debug("[MockSfuBackend] createWebRtcTransport: routerId={} transportId={}",
                  routerId, transportId);
    return Result<TransportInfo>::ok(std::move(info));
}

Result<void> MockSfuBackend::connectTransport(const std::string& transportId,
                                               const nlohmann::json& dtlsParameters)
{
    auto it = m_transports.find(transportId);
    if (it == m_transports.end()) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Transport not found: " + transportId});
    }
    it->second.connected = true;
    spdlog::debug("[MockSfuBackend] connectTransport: transportId={} dtlsRole={}",
                  transportId,
                  dtlsParameters.value("role", "<none>"));
    return Result<void>::ok();
}

void MockSfuBackend::closeTransport(const std::string& transportId)
{
    m_transports.erase(transportId);
    spdlog::debug("[MockSfuBackend] closeTransport: transportId={}", transportId);
}

// ---------------------------------------------------------------------------
// Producer
// ---------------------------------------------------------------------------

Result<ProducerInfo> MockSfuBackend::produce(const std::string& transportId,
                                              const std::string& kind,
                                              const nlohmann::json& /*rtpParameters*/)
{
    if (m_transports.find(transportId) == m_transports.end()) {
        return Result<ProducerInfo>::fail(
            ApiError{ErrorCode::NotFound, "Transport not found: " + transportId});
    }
    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        spdlog::error("[MockSfuBackend] produce failed: {}", uuidResult.error().message);
        return Result<ProducerInfo>::fail(
            ApiError{ErrorCode::InternalError, "produce failed: " + uuidResult.error().message});
    }
    const std::string producerId = uuidResult.take();
    m_producers[producerId] = ProducerState{ transportId, kind, false };
    ProducerInfo info;
    info.id   = producerId;
    info.kind = kind;
    info.type = "simple";
    spdlog::debug("[MockSfuBackend] produce: transportId={} kind={} producerId={}",
                  transportId, kind, producerId);
    return Result<ProducerInfo>::ok(std::move(info));
}

Result<void> MockSfuBackend::pauseProducer(const std::string& producerId)
{
    auto it = m_producers.find(producerId);
    if (it == m_producers.end()) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Producer not found: " + producerId});
    }
    it->second.paused = true;
    spdlog::debug("[MockSfuBackend] pauseProducer: producerId={}", producerId);
    return Result<void>::ok();
}

Result<void> MockSfuBackend::resumeProducer(const std::string& producerId)
{
    auto it = m_producers.find(producerId);
    if (it == m_producers.end()) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Producer not found: " + producerId});
    }
    it->second.paused = false;
    spdlog::debug("[MockSfuBackend] resumeProducer: producerId={}", producerId);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// Consumer
// ---------------------------------------------------------------------------

Result<ConsumerInfo> MockSfuBackend::consume(const std::string& transportId,
                                              const std::string& producerId,
                                              const nlohmann::json& /*rtpCapabilities*/)
{
    if (m_transports.find(transportId) == m_transports.end()) {
        return Result<ConsumerInfo>::fail(
            ApiError{ErrorCode::NotFound, "Transport not found: " + transportId});
    }

    auto prodIt = m_producers.find(producerId);
    if (prodIt == m_producers.end()) {
        return Result<ConsumerInfo>::fail(
            ApiError{ErrorCode::NotFound, "Producer not found: " + producerId});
    }

    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        spdlog::error("[MockSfuBackend] consume failed: {}", uuidResult.error().message);
        return Result<ConsumerInfo>::fail(
            ApiError{ErrorCode::InternalError, "consume failed: " + uuidResult.error().message});
    }
    const std::string consumerId = uuidResult.take();
    const std::string kind = prodIt->second.kind;
    m_consumers[consumerId] = ConsumerState{ transportId, producerId, kind };

    ConsumerInfo info;
    info.id         = consumerId;
    info.producerId = producerId;
    info.kind       = kind;
    // Minimal RTP parameters matching the codec kind
    if (kind == "video") {
        info.rtpParameters = {
            {"codecs", nlohmann::json::array({
                {
                    {"mimeType",        "video/H264"},
                    {"payloadType",     96},
                    {"clockRate",       90000},
                    {"parameters", {
                        {"packetization-mode", 1},
                        {"profile-level-id",   "42e01f"}
                    }}
                }
            })},
            {"encodings", nlohmann::json::array({
                {{"ssrc", 100000}}
            })}
        };
    } else {
        info.rtpParameters = {
            {"codecs", nlohmann::json::array({
                {
                    {"mimeType",    "audio/opus"},
                    {"payloadType", 111},
                    {"clockRate",   48000},
                    {"channels",    2}
                }
            })},
            {"encodings", nlohmann::json::array({
                {{"ssrc", 200000}}
            })}
        };
    }

    spdlog::debug("[MockSfuBackend] consume: transportId={} producerId={} consumerId={}",
                  transportId, producerId, consumerId);
    return Result<ConsumerInfo>::ok(std::move(info));
}

Result<void> MockSfuBackend::requestKeyFrame(const std::string& consumerId)
{
    if (m_consumers.find(consumerId) == m_consumers.end()) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Consumer not found: " + consumerId});
    }
    spdlog::debug("[MockSfuBackend] requestKeyFrame: consumerId={}", consumerId);
    return Result<void>::ok();
}

Result<void> MockSfuBackend::setConsumerPreferredLayers(const std::string& consumerId,
                                                        int spatialLayer, int temporalLayer)
{
    if (m_consumers.find(consumerId) == m_consumers.end()) {
        return Result<void>::fail(
            ApiError{ErrorCode::NotFound, "Consumer not found: " + consumerId});
    }
    spdlog::debug("[MockSfuBackend] setConsumerPreferredLayers: consumerId={} spatial={} temporal={}",
                  consumerId, spatialLayer, temporalLayer);
    return Result<void>::ok();
}

} // namespace hub32api::media
