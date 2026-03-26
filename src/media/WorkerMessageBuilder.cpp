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

// ---------------------------------------------------------------------------
// Helper: Build FBS RtpCodecParameters from JSON codec object.
// ---------------------------------------------------------------------------
namespace {

flatbuffers::Offset<FBS::RtpParameters::RtpCodecParameters>
buildCodecFromJson(flatbuffers::FlatBufferBuilder& builder, const nlohmann::json& codec)
{
    auto mimeType = builder.CreateString(codec.value("mimeType", ""));
    auto payloadType = static_cast<uint8_t>(codec.value("payloadType", 0));
    auto clockRate = codec.value("clockRate", 0u);
    auto channels = codec.contains("channels")
        ? flatbuffers::Optional<uint8_t>(static_cast<uint8_t>(codec["channels"].get<int>()))
        : flatbuffers::Optional<uint8_t>();

    // Build parameters
    std::vector<flatbuffers::Offset<FBS::RtpParameters::Parameter>> params;
    if (codec.contains("parameters") && codec["parameters"].is_object()) {
        for (auto& [key, val] : codec["parameters"].items()) {
            auto nameOffset = builder.CreateString(key);
            flatbuffers::Offset<void> valueOffset;
            FBS::RtpParameters::Value valueType;
            if (val.is_number_integer()) {
                auto intVal = FBS::RtpParameters::CreateInteger32(builder, val.get<int32_t>());
                valueType = FBS::RtpParameters::Value::Integer32;
                valueOffset = intVal.Union();
            } else if (val.is_number_float()) {
                auto dblVal = FBS::RtpParameters::CreateDouble(builder, val.get<double>());
                valueType = FBS::RtpParameters::Value::Double;
                valueOffset = dblVal.Union();
            } else {
                auto strOff = builder.CreateString(val.is_string() ? val.get<std::string>() : val.dump());
                auto strVal = FBS::RtpParameters::CreateString(builder, strOff);
                valueType = FBS::RtpParameters::Value::String;
                valueOffset = strVal.Union();
            }
            params.push_back(FBS::RtpParameters::CreateParameter(builder, nameOffset, valueType, valueOffset));
        }
    }
    auto paramsVec = builder.CreateVector(params);

    // Build rtcpFeedback
    std::vector<flatbuffers::Offset<FBS::RtpParameters::RtcpFeedback>> fbVec;
    if (codec.contains("rtcpFeedback") && codec["rtcpFeedback"].is_array()) {
        for (const auto& fb : codec["rtcpFeedback"]) {
            auto typeOff = builder.CreateString(fb.value("type", ""));
            auto paramOff = fb.contains("parameter")
                ? builder.CreateString(fb["parameter"].get<std::string>())
                : builder.CreateString("");
            fbVec.push_back(FBS::RtpParameters::CreateRtcpFeedback(builder, typeOff, paramOff));
        }
    }
    auto rtcpFbVec = builder.CreateVector(fbVec);

    return FBS::RtpParameters::CreateRtpCodecParameters(builder,
        mimeType, payloadType, clockRate, channels, paramsVec, rtcpFbVec);
}

// Map URI string to FBS enum
FBS::RtpParameters::RtpHeaderExtensionUri mapHeaderExtUri(const std::string& uri)
{
    if (uri.find("sdes:mid") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::Mid;
    if (uri.find("sdes:rtp-stream-id") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::RtpStreamId;
    if (uri.find("sdes:repaired-rtp-stream-id") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::RepairRtpStreamId;
    if (uri.find("abs-send-time") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::AbsSendTime;
    if (uri.find("transport-wide-cc") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::TransportWideCcDraft01;
    if (uri.find("ssrc-audio-level") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::SsrcAudioLevel;
    if (uri.find("dependency-descriptor") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::DependencyDescriptor;
    if (uri.find("video-orientation") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::VideoOrientation;
    if (uri.find("toffset") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::TimeOffset;
    if (uri.find("abs-capture-time") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::AbsCaptureTime;
    if (uri.find("playout-delay") != std::string::npos) return FBS::RtpParameters::RtpHeaderExtensionUri::PlayoutDelay;
    return FBS::RtpParameters::RtpHeaderExtensionUri::Mid; // fallback
}

// Build FBS RtpParameters from JSON
flatbuffers::Offset<FBS::RtpParameters::RtpParameters>
buildRtpParametersFromJson(flatbuffers::FlatBufferBuilder& builder, const nlohmann::json& json)
{
    // mid
    auto midOffset = json.contains("mid")
        ? builder.CreateString(json["mid"].get<std::string>())
        : builder.CreateString("");

    // codecs
    std::vector<flatbuffers::Offset<FBS::RtpParameters::RtpCodecParameters>> codecs;
    if (json.contains("codecs") && json["codecs"].is_array()) {
        for (const auto& c : json["codecs"]) {
            codecs.push_back(buildCodecFromJson(builder, c));
        }
    }
    auto codecsVec = builder.CreateVector(codecs);

    // headerExtensions
    std::vector<flatbuffers::Offset<FBS::RtpParameters::RtpHeaderExtensionParameters>> exts;
    if (json.contains("headerExtensions") && json["headerExtensions"].is_array()) {
        for (const auto& ext : json["headerExtensions"]) {
            auto uri = mapHeaderExtUri(ext.value("uri", ""));
            auto id = static_cast<uint8_t>(ext.value("id", 0));
            bool encrypt = ext.value("encrypt", false);
            std::vector<flatbuffers::Offset<FBS::RtpParameters::Parameter>> extParams;
            exts.push_back(FBS::RtpParameters::CreateRtpHeaderExtensionParameters(
                builder, uri, id, encrypt, builder.CreateVector(extParams)));
        }
    }
    auto extsVec = builder.CreateVector(exts);

    // encodings
    std::vector<flatbuffers::Offset<FBS::RtpParameters::RtpEncodingParameters>> encs;
    if (json.contains("encodings") && json["encodings"].is_array()) {
        for (const auto& enc : json["encodings"]) {
            auto ssrc = enc.contains("ssrc")
                ? flatbuffers::Optional<uint32_t>(enc["ssrc"].get<uint32_t>())
                : flatbuffers::Optional<uint32_t>();
            auto ridOffset = enc.contains("rid")
                ? builder.CreateString(enc["rid"].get<std::string>())
                : builder.CreateString("");
            auto codecPt = enc.contains("codecPayloadType")
                ? flatbuffers::Optional<uint8_t>(static_cast<uint8_t>(enc["codecPayloadType"].get<int>()))
                : flatbuffers::Optional<uint8_t>();

            flatbuffers::Offset<FBS::RtpParameters::Rtx> rtxOffset = 0;
            if (enc.contains("rtx") && enc["rtx"].is_object() && enc["rtx"].contains("ssrc")) {
                rtxOffset = FBS::RtpParameters::CreateRtx(builder, enc["rtx"]["ssrc"].get<uint32_t>());
            }

            bool dtx = enc.value("dtx", false);
            auto scalMode = enc.contains("scalabilityMode")
                ? builder.CreateString(enc["scalabilityMode"].get<std::string>())
                : builder.CreateString("");
            auto maxBitrate = enc.contains("maxBitrate")
                ? flatbuffers::Optional<uint32_t>(enc["maxBitrate"].get<uint32_t>())
                : flatbuffers::Optional<uint32_t>();

            encs.push_back(FBS::RtpParameters::CreateRtpEncodingParameters(
                builder, ssrc, ridOffset, codecPt, rtxOffset, dtx, scalMode, maxBitrate));
        }
    }
    auto encsVec = builder.CreateVector(encs);

    // rtcp
    auto cnameOffset = builder.CreateString(
        (json.contains("rtcp") && json["rtcp"].contains("cname"))
            ? json["rtcp"]["cname"].get<std::string>() : "");
    bool reducedSize = (json.contains("rtcp") && json["rtcp"].contains("reducedSize"))
        ? json["rtcp"]["reducedSize"].get<bool>() : true;
    auto rtcpOffset = FBS::RtpParameters::CreateRtcpParameters(builder, cnameOffset, reducedSize);

    // msid (optional, not commonly used)

    return FBS::RtpParameters::CreateRtpParameters(
        builder, midOffset, codecsVec, extsVec, encsVec, rtcpOffset);
}

// Build FBS RtpMapping from JSON rtpParameters.
// Server-side mediasoup needs an RtpMapping that maps client codecs
// to server-side payload types and SSRCs. This builds a minimal 1:1 mapping.
flatbuffers::Offset<FBS::RtpParameters::RtpMapping>
buildRtpMappingFromJson(flatbuffers::FlatBufferBuilder& builder, const nlohmann::json& rtpParams)
{
    // Codec mappings: map each codec payload type to itself (1:1)
    std::vector<flatbuffers::Offset<FBS::RtpParameters::CodecMapping>> codecMappings;
    if (rtpParams.contains("codecs") && rtpParams["codecs"].is_array()) {
        for (const auto& c : rtpParams["codecs"]) {
            auto pt = static_cast<uint8_t>(c.value("payloadType", 0));
            codecMappings.push_back(
                FBS::RtpParameters::CreateCodecMapping(builder, pt, pt));
        }
    }
    auto codecMappingsVec = builder.CreateVector(codecMappings);

    // Encoding mappings: map each encoding SSRC to a generated mapped SSRC
    std::vector<flatbuffers::Offset<FBS::RtpParameters::EncodingMapping>> encMappings;
    if (rtpParams.contains("encodings") && rtpParams["encodings"].is_array()) {
        uint32_t mappedSsrc = 100000000; // starting mapped SSRC
        for (const auto& enc : rtpParams["encodings"]) {
            auto ridOff = enc.contains("rid")
                ? builder.CreateString(enc["rid"].get<std::string>())
                : builder.CreateString("");
            auto ssrc = enc.contains("ssrc")
                ? flatbuffers::Optional<uint32_t>(enc["ssrc"].get<uint32_t>())
                : flatbuffers::Optional<uint32_t>();
            auto scalMode = enc.contains("scalabilityMode")
                ? builder.CreateString(enc["scalabilityMode"].get<std::string>())
                : builder.CreateString("");
            encMappings.push_back(FBS::RtpParameters::CreateEncodingMapping(
                builder, ridOff, ssrc, scalMode, mappedSsrc));
            mappedSsrc++;
        }
    }
    auto encMappingsVec = builder.CreateVector(encMappings);

    return FBS::RtpParameters::CreateRtpMapping(builder, codecMappingsVec, encMappingsVec);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// createProduceRequest
// ---------------------------------------------------------------------------
std::vector<uint8_t> WorkerMessageBuilder::createProduceRequest(
    uint32_t requestId,
    const std::string& transportId,
    const std::string& producerId,
    const std::string& kind,
    const nlohmann::json& rtpParameters)
{
    flatbuffers::FlatBufferBuilder builder(2048);  // 2048 bytes initial buffer

    auto producerIdOffset = builder.CreateString(producerId);
    auto mediaKind = (kind == "audio")
        ? FBS::RtpParameters::MediaKind::AUDIO
        : FBS::RtpParameters::MediaKind::VIDEO;

    auto rtpParamsOffset = buildRtpParametersFromJson(builder, rtpParameters);
    auto rtpMappingOffset = buildRtpMappingFromJson(builder, rtpParameters);

    auto bodyOffset = FBS::Transport::CreateProduceRequest(builder,
        producerIdOffset,
        mediaKind,
        rtpParamsOffset,
        rtpMappingOffset,
        /* paused */ false,
        /* keyFrameRequestDelay */ 0,
        /* enableMediasoupPacketIdHeaderExtension */ false);

    auto handlerIdOffset = builder.CreateString(transportId);
    auto requestOffset = FBS::Request::CreateRequest(builder,
        requestId,
        FBS::Request::Method::TRANSPORT_PRODUCE,
        handlerIdOffset,
        FBS::Request::Body::Transport_ProduceRequest,
        bodyOffset.Union());

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

// ---------------------------------------------------------------------------
// createConsumeRequest
// ---------------------------------------------------------------------------
std::vector<uint8_t> WorkerMessageBuilder::createConsumeRequest(
    uint32_t requestId,
    const std::string& transportId,
    const std::string& consumerId,
    const std::string& producerId,
    const nlohmann::json& rtpCapabilities)
{
    flatbuffers::FlatBufferBuilder builder(2048);  // 2048 bytes initial buffer

    auto consumerIdOffset = builder.CreateString(consumerId);
    auto producerIdOffset = builder.CreateString(producerId);

    // Build RTP parameters from capabilities (server decides actual params)
    auto rtpParamsOffset = buildRtpParametersFromJson(builder, rtpCapabilities);

    // Consumable RTP encodings: at minimum one encoding with a mapped SSRC
    std::vector<flatbuffers::Offset<FBS::RtpParameters::RtpEncodingParameters>> consumableEncs;
    {
        auto enc = FBS::RtpParameters::CreateRtpEncodingParameters(builder,
            flatbuffers::Optional<uint32_t>(200000000), // ssrc
            builder.CreateString(""),                   // rid
            flatbuffers::Optional<uint8_t>(),           // codecPayloadType
            0,                                          // rtx
            false,                                      // dtx
            builder.CreateString(""),                   // scalabilityMode
            flatbuffers::Optional<uint32_t>());         // maxBitrate
        consumableEncs.push_back(enc);
    }
    auto consumableEncsVec = builder.CreateVector(consumableEncs);

    auto bodyOffset = FBS::Transport::CreateConsumeRequest(builder,
        consumerIdOffset,
        producerIdOffset,
        FBS::RtpParameters::MediaKind::VIDEO, // kind
        rtpParamsOffset,
        FBS::RtpParameters::Type::SIMPLE,     // type
        consumableEncsVec,
        /* paused */ false,
        /* preferredLayers */ 0,
        /* ignoreDtx */ false);

    auto handlerIdOffset = builder.CreateString(transportId);
    auto requestOffset = FBS::Request::CreateRequest(builder,
        requestId,
        FBS::Request::Method::TRANSPORT_CONSUME,
        handlerIdOffset,
        FBS::Request::Body::Transport_ConsumeRequest,
        bodyOffset.Union());

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

// ---------------------------------------------------------------------------
// createPauseProducerRequest
// ---------------------------------------------------------------------------
std::vector<uint8_t> WorkerMessageBuilder::createPauseProducerRequest(
    uint32_t requestId, const std::string& producerId)
{
    flatbuffers::FlatBufferBuilder builder(128);  // 128 bytes initial buffer

    auto handlerIdOffset = builder.CreateString(producerId);
    auto requestOffset = FBS::Request::CreateRequest(builder,
        requestId,
        FBS::Request::Method::PRODUCER_PAUSE,
        handlerIdOffset,
        FBS::Request::Body::NONE, 0);

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

// ---------------------------------------------------------------------------
// createResumeProducerRequest
// ---------------------------------------------------------------------------
std::vector<uint8_t> WorkerMessageBuilder::createResumeProducerRequest(
    uint32_t requestId, const std::string& producerId)
{
    flatbuffers::FlatBufferBuilder builder(128);  // 128 bytes initial buffer

    auto handlerIdOffset = builder.CreateString(producerId);
    auto requestOffset = FBS::Request::CreateRequest(builder,
        requestId,
        FBS::Request::Method::PRODUCER_RESUME,
        handlerIdOffset,
        FBS::Request::Body::NONE, 0);

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

// ---------------------------------------------------------------------------
// createRequestKeyFrameRequest
// ---------------------------------------------------------------------------
std::vector<uint8_t> WorkerMessageBuilder::createRequestKeyFrameRequest(
    uint32_t requestId, const std::string& consumerId)
{
    flatbuffers::FlatBufferBuilder builder(128);  // 128 bytes initial buffer

    auto handlerIdOffset = builder.CreateString(consumerId);
    auto requestOffset = FBS::Request::CreateRequest(builder,
        requestId,
        FBS::Request::Method::CONSUMER_REQUEST_KEY_FRAME,
        handlerIdOffset,
        FBS::Request::Body::NONE, 0);

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

// ---------------------------------------------------------------------------
// createSetPreferredLayersRequest
// ---------------------------------------------------------------------------
std::vector<uint8_t> WorkerMessageBuilder::createSetPreferredLayersRequest(
    uint32_t requestId, const std::string& consumerId,
    int spatialLayer, int temporalLayer)
{
    flatbuffers::FlatBufferBuilder builder(256);  // 256 bytes initial buffer

    // SetPreferredLayersRequest requires a ConsumerLayers table
    auto layersOffset = FBS::Consumer::CreateConsumerLayers(builder,
        static_cast<uint8_t>(spatialLayer),
        flatbuffers::Optional<uint8_t>(static_cast<uint8_t>(temporalLayer)));
    auto bodyOffset = FBS::Consumer::CreateSetPreferredLayersRequest(builder, layersOffset);

    auto handlerIdOffset = builder.CreateString(consumerId);
    auto requestOffset = FBS::Request::CreateRequest(builder,
        requestId,
        FBS::Request::Method::CONSUMER_SET_PREFERRED_LAYERS,
        handlerIdOffset,
        FBS::Request::Body::Consumer_SetPreferredLayersRequest,
        bodyOffset.Union());

    auto messageOffset = FBS::Message::CreateMessage(builder,
        FBS::Message::Body::Request,
        requestOffset.Union());

    builder.FinishSizePrefixed(messageOffset);
    auto* buf = builder.GetBufferPointer();
    return {buf, buf + builder.GetSize()};
}

// ---------------------------------------------------------------------------
// parseProduceResponse
// ---------------------------------------------------------------------------
bool WorkerMessageBuilder::parseProduceResponse(
    const std::vector<uint8_t>& data,
    uint32_t expectedId,
    std::string& type)
{
    if (data.empty()) return false;

    const auto* message = FBS::Message::GetMessage(data.data());
    if (message->data_type() != FBS::Message::Body::Response) return false;

    const auto* response = message->data_as_Response();
    if (response->id() != expectedId || !response->accepted()) return false;

    const auto* produceResp = response->body_as_Transport_ProduceResponse();
    if (!produceResp) {
        type = "simple";
        return true;
    }

    switch (produceResp->type()) {
        case FBS::RtpParameters::Type::SIMPLE:    type = "simple"; break;
        case FBS::RtpParameters::Type::SIMULCAST:  type = "simulcast"; break;
        case FBS::RtpParameters::Type::SVC:        type = "svc"; break;
        case FBS::RtpParameters::Type::PIPE:       type = "pipe"; break;
        default:                                    type = "simple"; break;
    }
    return true;
}

// ---------------------------------------------------------------------------
// parseConsumeResponse
// ---------------------------------------------------------------------------
nlohmann::json WorkerMessageBuilder::parseConsumeResponse(
    const std::vector<uint8_t>& responseBody)
{
    nlohmann::json result;

    if (responseBody.empty()) return result;

    const auto* message = FBS::Message::GetMessage(responseBody.data());
    const auto* response = message->data_as_Response();
    const auto* consumeResp = response->body_as_Transport_ConsumeResponse();

    if (!consumeResp) return result;

    result["paused"] = consumeResp->paused();
    result["producerPaused"] = consumeResp->producer_paused();

    if (consumeResp->preferred_layers()) {
        result["preferredLayers"]["spatialLayer"] = consumeResp->preferred_layers()->spatial_layer();
        result["preferredLayers"]["temporalLayer"] = consumeResp->preferred_layers()->temporal_layer();
    }

    return result;
}

} // namespace hub32api::media

#endif // HUB32_WITH_MEDIASOUP
