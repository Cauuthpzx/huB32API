#ifdef HUB32_WITH_MEDIASOUP

#include "../core/PrecompiledHeader.hpp"
#include "WorkerMessageBuilder.hpp"

namespace hub32api::media {

std::vector<uint8_t> WorkerMessageBuilder::createRouterRequest(
    uint32_t requestId, const std::string& routerId)
{
    flatbuffers::FlatBufferBuilder builder(256);  // 256 bytes initial buffer

    auto routerIdOffset = builder.CreateString(routerId);
    auto bodyOffset = FBS::Worker::CreateCreateRouterRequest(builder, routerIdOffset);

    auto handlerIdOffset = builder.CreateString("");
    auto requestOffset = FBS::Request::CreateRequest(
        builder,
        requestId,
        FBS::Request::Method::WORKER_CREATE_ROUTER,
        handlerIdOffset,
        FBS::Request::Body::Worker_CreateRouterRequest,
        bodyOffset.Union());

    auto messageOffset = FBS::Message::CreateMessage(
        builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

std::vector<uint8_t> WorkerMessageBuilder::closeRouterRequest(
    uint32_t requestId, const std::string& routerId)
{
    flatbuffers::FlatBufferBuilder builder(128);  // 128 bytes initial buffer

    auto handlerIdOffset = builder.CreateString(routerId);
    auto requestOffset = FBS::Request::CreateRequest(
        builder,
        requestId,
        FBS::Request::Method::ROUTER_CLOSE,
        handlerIdOffset,
        FBS::Request::Body::NONE, 0);

    auto messageOffset = FBS::Message::CreateMessage(
        builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

std::vector<uint8_t> WorkerMessageBuilder::createWebRtcTransportRequest(
    uint32_t requestId,
    const std::string& routerId,
    const std::string& transportId,
    const std::string& listenIp,
    int rtcMinPort, int rtcMaxPort)
{
    flatbuffers::FlatBufferBuilder builder(1024);  // 1024 bytes initial buffer

    // Build listen info: UDP socket on listenIp with port range
    auto portRange = FBS::Transport::CreatePortRange(builder,
        static_cast<uint16_t>(rtcMinPort),
        static_cast<uint16_t>(rtcMaxPort));
    auto socketFlags = FBS::Transport::CreateSocketFlags(builder, false, false);
    auto ipOffset = builder.CreateString(listenIp);

    auto listenInfo = FBS::Transport::CreateListenInfo(builder,
        FBS::Transport::Protocol::UDP,
        ipOffset, ipOffset,  // ip and announcedAddress
        0,                   // port (0 = auto-assign within range)
        portRange, socketFlags,
        0, 0);               // sendBufferSize=0, recvBufferSize=0 (OS defaults)

    std::vector<flatbuffers::Offset<FBS::Transport::ListenInfo>> listenInfos = {listenInfo};
    auto listenInfosVec = builder.CreateVector(listenInfos);
    auto listenIndividual = FBS::WebRtcTransport::CreateListenIndividual(builder, listenInfosVec);

    // Build base transport options
    auto numSctpStreams = FBS::SctpParameters::CreateNumSctpStreams(
        builder,
        1024,    // OS (outgoing streams)
        1024);   // MIS (max incoming streams)
    auto baseOptions = FBS::Transport::CreateOptions(builder,
        false,                               // direct
        flatbuffers::Optional<uint32_t>(),   // maxMessageSize (unset = default)
        600000,                              // initialAvailableOutgoingBitrate (600 kbps)
        false,                               // enableSctp
        numSctpStreams,
        262144,                              // maxSctpMessageSize (256 KB)
        262144,                              // sctpSendBufferSize (256 KB)
        false);                              // isDataChannel

    // Build WebRTC transport options
    auto webRtcOptions = FBS::WebRtcTransport::CreateWebRtcTransportOptions(builder,
        baseOptions,
        FBS::WebRtcTransport::Listen::ListenIndividual,
        listenIndividual.Union(),
        true,   // enableUdp
        true,   // enableTcp
        true,   // preferUdp
        false,  // preferTcp
        30);    // iceConsentTimeout (30 seconds)

    // Build router create-transport request
    auto transportIdOffset = builder.CreateString(transportId);
    auto routerReq = FBS::Router::CreateCreateWebRtcTransportRequest(builder,
        transportIdOffset, webRtcOptions);

    auto handlerIdOffset = builder.CreateString(routerId);
    auto requestOffset = FBS::Request::CreateRequest(builder,
        requestId,
        FBS::Request::Method::ROUTER_CREATE_WEBRTCTRANSPORT,
        handlerIdOffset,
        FBS::Request::Body::Router_CreateWebRtcTransportRequest,
        routerReq.Union());

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

std::vector<uint8_t> WorkerMessageBuilder::connectTransportRequest(
    uint32_t requestId,
    const std::string& transportId,
    const nlohmann::json& dtlsParams)
{
    flatbuffers::FlatBufferBuilder builder(512);  // 512 bytes initial buffer

    // Build DTLS fingerprints from JSON
    std::vector<flatbuffers::Offset<FBS::WebRtcTransport::Fingerprint>> fingerprints;
    if (dtlsParams.contains("fingerprints")) {
        for (const auto& fp : dtlsParams["fingerprints"]) {
            auto algo = FBS::WebRtcTransport::FingerprintAlgorithm::SHA256;
            const auto algoStr = fp.value("algorithm", "sha-256");

            if (algoStr == "sha-1" || algoStr == "SHA1") {
                algo = FBS::WebRtcTransport::FingerprintAlgorithm::SHA1;
            } else if (algoStr == "sha-256" || algoStr == "SHA256") {
                algo = FBS::WebRtcTransport::FingerprintAlgorithm::SHA256;
            } else if (algoStr == "sha-384" || algoStr == "SHA384") {
                algo = FBS::WebRtcTransport::FingerprintAlgorithm::SHA384;
            } else if (algoStr == "sha-512" || algoStr == "SHA512") {
                algo = FBS::WebRtcTransport::FingerprintAlgorithm::SHA512;
            }

            auto valueOffset = builder.CreateString(fp["value"].get<std::string>());
            fingerprints.push_back(
                FBS::WebRtcTransport::CreateFingerprint(builder, algo, valueOffset));
        }
    }
    auto fingerprintsVec = builder.CreateVector(fingerprints);

    // Parse DTLS role
    auto role = FBS::WebRtcTransport::DtlsRole::AUTO;
    if (dtlsParams.contains("role")) {
        const auto roleStr = dtlsParams["role"].get<std::string>();
        if (roleStr == "client") {
            role = FBS::WebRtcTransport::DtlsRole::CLIENT;
        } else if (roleStr == "server") {
            role = FBS::WebRtcTransport::DtlsRole::SERVER;
        }
    }

    auto dtlsParamsOffset = FBS::WebRtcTransport::CreateDtlsParameters(builder,
        fingerprintsVec, role);
    auto connectReq = FBS::WebRtcTransport::CreateConnectRequest(builder, dtlsParamsOffset);

    auto handlerIdOffset = builder.CreateString(transportId);
    auto requestOffset = FBS::Request::CreateRequest(builder,
        requestId,
        FBS::Request::Method::WEBRTCTRANSPORT_CONNECT,
        handlerIdOffset,
        FBS::Request::Body::WebRtcTransport_ConnectRequest,
        connectReq.Union());

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

std::vector<uint8_t> WorkerMessageBuilder::closeTransportNotification(
    const std::string& transportId)
{
    flatbuffers::FlatBufferBuilder builder(128);  // 128 bytes initial buffer

    auto handlerIdOffset = builder.CreateString(transportId);
    auto notifOffset = FBS::Notification::CreateNotification(builder,
        handlerIdOffset,
        FBS::Notification::Event::TRANSPORT_CLOSE,
        FBS::Notification::Body::NONE, 0);

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Notification,
        notifOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

std::vector<uint8_t> WorkerMessageBuilder::closeWorkerRequest(uint32_t requestId)
{
    flatbuffers::FlatBufferBuilder builder(128);  // 128 bytes initial buffer

    auto handlerIdOffset = builder.CreateString("");
    auto requestOffset = FBS::Request::CreateRequest(builder,
        requestId,
        FBS::Request::Method::WORKER_CLOSE,
        handlerIdOffset,
        FBS::Request::Body::NONE, 0);

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

bool WorkerMessageBuilder::parseResponse(
    const std::vector<uint8_t>& data,
    uint32_t expectedId,
    std::vector<uint8_t>& responseBody,
    std::string& errorReason)
{
    if (data.empty()) {
        errorReason = "empty response";
        return false;
    }

    const auto* message = FBS::Message::GetMessage(data.data());
    if (message->data_type() != FBS::Message::Body::Response) {
        errorReason = "not a response message";
        return false;
    }

    const auto* response = message->data_as_Response();
    if (response->id() != expectedId) {
        errorReason = "response ID mismatch: expected " + std::to_string(expectedId)
                    + " got " + std::to_string(response->id());
        return false;
    }

    if (!response->accepted()) {
        errorReason = response->reason() ? response->reason()->str() : "unknown error";
        return false;
    }

    // Copy full response bytes for caller to parse type-specific body
    responseBody = data;
    return true;
}

nlohmann::json WorkerMessageBuilder::parseWebRtcTransportDump(
    const std::vector<uint8_t>& responseBody)
{
    nlohmann::json result;

    const auto* message = FBS::Message::GetMessage(responseBody.data());
    const auto* response = message->data_as_Response();
    const auto* dump = response->body_as_WebRtcTransport_DumpResponse();

    if (!dump) {
        return result;
    }

    // Extract ICE parameters
    if (dump->ice_parameters()) {
        result["iceParameters"]["usernameFragment"] =
            dump->ice_parameters()->username_fragment()
                ? dump->ice_parameters()->username_fragment()->str() : "";
        result["iceParameters"]["password"] =
            dump->ice_parameters()->password()
                ? dump->ice_parameters()->password()->str() : "";
        result["iceParameters"]["iceLite"] = dump->ice_parameters()->ice_lite();
    }

    // Extract ICE candidates
    result["iceCandidates"] = nlohmann::json::array();
    if (dump->ice_candidates()) {
        for (const auto* candidate : *dump->ice_candidates()) {
            nlohmann::json c;
            c["foundation"] = candidate->foundation()
                ? candidate->foundation()->str() : "";
            c["priority"] = candidate->priority();
            c["address"] = candidate->address()
                ? candidate->address()->str() : "";
            c["port"] = candidate->port();
            c["protocol"] = (candidate->protocol() == FBS::Transport::Protocol::UDP)
                ? "udp" : "tcp";
            c["type"] = "host";
            result["iceCandidates"].push_back(std::move(c));
        }
    }

    // Extract DTLS parameters
    if (dump->dtls_parameters()) {
        result["dtlsParameters"]["role"] = "auto";
        result["dtlsParameters"]["fingerprints"] = nlohmann::json::array();
        if (dump->dtls_parameters()->fingerprints()) {
            for (const auto* fp : *dump->dtls_parameters()->fingerprints()) {
                nlohmann::json f;
                // TODO: Map FBS::WebRtcTransport::FingerprintAlgorithm enum to string
                f["algorithm"] = "sha-256";
                f["value"] = fp->value() ? fp->value()->str() : "";
                result["dtlsParameters"]["fingerprints"].push_back(std::move(f));
            }
        }
    }

    return result;
}

} // namespace hub32api::media

#endif // HUB32_WITH_MEDIASOUP
