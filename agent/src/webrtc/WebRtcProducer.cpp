#ifdef HUB32_WITH_WEBRTC

#include "hub32agent/webrtc/WebRtcProducer.hpp"
#include "hub32agent/webrtc/SignalingClient.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>

// libdatachannel
#include <rtc/rtc.hpp>

namespace hub32agent::webrtc {

// -----------------------------------------------------------------------
// RTP constants
// -----------------------------------------------------------------------
constexpr size_t   kRtpHeaderSize  = 12;     // bytes — fixed RTP header
constexpr size_t   kRtpMtu         = 1200;   // bytes — conservative MTU
constexpr size_t   kMaxPayload     = kRtpMtu - kRtpHeaderSize;  // 1188
constexpr size_t   kFuHeaderSize   = 2;      // bytes — FU-A indicator + header
constexpr uint8_t  kH264NalFuA    = 28;     // NAL unit type for FU-A fragmentation
constexpr uint8_t  kH264NalStapA  = 24;     // NAL unit type for STAP-A aggregation
constexpr uint32_t kRtpClockRate  = 90000;  // Hz — RTP clock for H.264 video
constexpr uint8_t  kRtpPayloadType = 96;    // dynamic payload type for H.264
constexpr uint8_t  kRtpVersion     = 2;     // RTP version
constexpr size_t   kStapSmallLimit = 600;   // NAL ≤ this → candidate for STAP-A

struct WebRtcProducer::Impl
{
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> videoTrack;
    uint16_t rtpSeqNum = 0;   ///< RTP sequence number (wraps at 65535)
    uint32_t rtpSsrc   = 1;   ///< RTP synchronization source identifier
};

WebRtcProducer::WebRtcProducer(SignalingClient& signaling, const Config& config)
    : m_signaling(signaling)
    , m_config(config)
    , m_impl(std::make_unique<Impl>())
{
}

WebRtcProducer::~WebRtcProducer()
{
    disconnect();
}

bool WebRtcProducer::connect()
{
    spdlog::info("[WebRtcProducer] connecting to location '{}'", m_config.locationId);

    // 1. Get ICE servers
    auto iceServers = m_signaling.getIceServers();

    // 2. Configure PeerConnection
    rtc::Configuration rtcConfig;
    for (const auto& srv : iceServers) {
        for (const auto& url : srv.urls) {
            rtc::IceServer iceSrv(url);
            if (!srv.username.empty()) {
                iceSrv.username = srv.username;
                iceSrv.password = srv.credential;
            }
            rtcConfig.iceServers.push_back(std::move(iceSrv));
        }
    }

    // 3. Create PeerConnection
    m_impl->pc = std::make_shared<rtc::PeerConnection>(rtcConfig);

    m_impl->pc->onStateChange([this](rtc::PeerConnection::State state) {
        std::string stateStr;
        switch (state) {
            case rtc::PeerConnection::State::New:          stateStr = "new"; break;
            case rtc::PeerConnection::State::Connecting:   stateStr = "connecting"; break;
            case rtc::PeerConnection::State::Connected:    stateStr = "connected"; break;
            case rtc::PeerConnection::State::Disconnected: stateStr = "disconnected"; break;
            case rtc::PeerConnection::State::Failed:       stateStr = "failed"; break;
            case rtc::PeerConnection::State::Closed:       stateStr = "closed"; break;
        }
        spdlog::info("[WebRtcProducer] state: {}", stateStr);
        if (m_stateCb) m_stateCb(stateStr);

        if (state == rtc::PeerConnection::State::Connected) {
            m_connected = true;
            m_reconnectAttempts = 0;
        } else if (state == rtc::PeerConnection::State::Failed ||
                   state == rtc::PeerConnection::State::Disconnected) {
            m_connected = false;
            attemptReconnect();
        }
    });

    // 4. Create H.264 video track
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    m_impl->videoTrack = m_impl->pc->addTrack(media);

    // 5. Create transport via signaling
    auto transport = m_signaling.createTransport(m_config.locationId, "send");
    if (!transport) {
        spdlog::error("[WebRtcProducer] failed to create transport");
        return false;
    }
    m_transportId = transport->id;
    spdlog::info("[WebRtcProducer] transport created: {}", m_transportId);

    // 6. Set remote ICE candidates from transport response
    if (transport->iceCandidates.is_array()) {
        for (const auto& candidate : transport->iceCandidates) {
            if (candidate.contains("candidate")) {
                std::string sdpCandidate = candidate["candidate"].get<std::string>();
                std::string sdpMid = candidate.value("sdpMid", "0");
                m_impl->pc->addRemoteCandidate(rtc::Candidate(sdpCandidate, sdpMid));
            }
        }
        spdlog::info("[WebRtcProducer] set {} remote ICE candidates",
                     transport->iceCandidates.size());
    }

    // 7. Connect transport with local DTLS parameters
    auto localDesc = m_impl->pc->localDescription();
    nlohmann::json dtlsParams;
    if (localDesc.has_value()) {
        std::string sdp = localDesc->generateSdp();
        std::string fingerprint;
        auto fpPos = sdp.find("a=fingerprint:");
        if (fpPos != std::string::npos) {
            auto lineEnd = sdp.find('\n', fpPos);
            fingerprint = sdp.substr(fpPos + 14,
                                      lineEnd != std::string::npos ? lineEnd - fpPos - 14 : std::string::npos);
            while (!fingerprint.empty() &&
                   (fingerprint.back() == '\r' || fingerprint.back() == '\n' || fingerprint.back() == ' ')) {
                fingerprint.pop_back();
            }
        }

        std::string algorithm = "sha-256";
        std::string hash = fingerprint;
        auto spacePos = fingerprint.find(' ');
        if (spacePos != std::string::npos) {
            algorithm = fingerprint.substr(0, spacePos);
            hash = fingerprint.substr(spacePos + 1);
        }

        dtlsParams["fingerprints"] = nlohmann::json::array({
            {{"algorithm", algorithm}, {"value", hash}}
        });
        dtlsParams["role"] = "client";
    }

    if (!m_signaling.connectTransport(m_transportId, dtlsParams)) {
        spdlog::error("[WebRtcProducer] failed to connect transport DTLS");
        return false;
    }
    spdlog::info("[WebRtcProducer] transport DTLS connected");

    // 8. Create producer with video RTP parameters
    nlohmann::json rtpParams;
    rtpParams["codecs"] = nlohmann::json::array({
        {
            {"mimeType", "video/H264"},
            {"payloadType", kRtpPayloadType},
            {"clockRate", kRtpClockRate},
            {"parameters", {
                {"packetization-mode", 1},
                {"profile-level-id", "42e01f"},
                {"level-asymmetry-allowed", 1}
            }}
        }
    });
    rtpParams["encodings"] = nlohmann::json::array({
        {{"ssrc", m_impl->rtpSsrc}}
    });

    auto producerId = m_signaling.produce(m_transportId, "video", rtpParams);
    if (producerId.empty()) {
        spdlog::error("[WebRtcProducer] failed to create producer");
        return false;
    }
    m_producerId = producerId;
    spdlog::info("[WebRtcProducer] producer created: {}", m_producerId);

    m_connected = true;
    return true;
}

// -----------------------------------------------------------------------
// NAL unit splitting — Annex B start codes (0x000001 or 0x00000001)
// -----------------------------------------------------------------------
struct NalSpan { const uint8_t* data; size_t size; };

static std::vector<NalSpan> find_nal_units(const uint8_t* buf, size_t len)
{
    std::vector<NalSpan> nals;
    if (!buf || len == 0) return nals;
    size_t i = 0;
    while (i < len) {
        size_t sc_len = 0;
        if (i + 3 < len && buf[i] == 0 && buf[i + 1] == 0 &&
            buf[i + 2] == 0 && buf[i + 3] == 1)
            sc_len = 4;
        else if (i + 2 < len && buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1)
            sc_len = 3;
        else { ++i; continue; }

        size_t nal_start = i + sc_len;
        size_t j = nal_start + 1;
        while (j < len) {
            if (j + 3 < len && buf[j] == 0 && buf[j + 1] == 0 &&
                buf[j + 2] == 0 && buf[j + 3] == 1) break;
            if (j + 2 < len && buf[j] == 0 && buf[j + 1] == 0 && buf[j + 2] == 1) break;
            ++j;
        }
        if (nal_start < len)
            nals.push_back({buf + nal_start, j - nal_start});
        i = j;
    }
    return nals;
}

// -----------------------------------------------------------------------
// build_rtp_header — writes 12-byte RTP header into dst
// -----------------------------------------------------------------------
static void build_rtp_header(uint8_t* dst, bool marker,
                              uint16_t seq, uint32_t ts, uint32_t ssrc)
{
    dst[0]  = (kRtpVersion << 6);   // V=2, P=0, X=0, CC=0
    dst[1]  = static_cast<uint8_t>((marker ? 0x80 : 0) | (kRtpPayloadType & 0x7F));
    dst[2]  = static_cast<uint8_t>(seq >> 8);
    dst[3]  = static_cast<uint8_t>(seq & 0xFF);
    dst[4]  = static_cast<uint8_t>(ts >> 24);
    dst[5]  = static_cast<uint8_t>((ts >> 16) & 0xFF);
    dst[6]  = static_cast<uint8_t>((ts >>  8) & 0xFF);
    dst[7]  = static_cast<uint8_t>(ts & 0xFF);
    dst[8]  = static_cast<uint8_t>(ssrc >> 24);
    dst[9]  = static_cast<uint8_t>((ssrc >> 16) & 0xFF);
    dst[10] = static_cast<uint8_t>((ssrc >>  8) & 0xFF);
    dst[11] = static_cast<uint8_t>(ssrc & 0xFF);
}

// -----------------------------------------------------------------------
// send_one_rtp — sends a single already-built RTP packet with pacing
// -----------------------------------------------------------------------
static void send_one_rtp(rtc::Track* track,
                          const std::vector<uint8_t>& pkt,
                          int gap_us)
{
    track->send(reinterpret_cast<const std::byte*>(pkt.data()), pkt.size());
    if (gap_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(gap_us));
    }
}

// -----------------------------------------------------------------------
// sendH264 — RTP packetization with SPS/PPS injection, STAP-A, pacing
// -----------------------------------------------------------------------

void WebRtcProducer::sendH264(const uint8_t* data, size_t size,
                                int64_t timestampUs, bool isKeyFrame)
{
    if (!m_connected || !m_impl->videoTrack) return;
    if (!data || size == 0) return;

    const uint32_t rtp_ts = static_cast<uint32_t>(timestampUs * 90 / 1000);

    // ---- MUST FIX #3: RTP pacing — inter-packet gap in µs ----
    const int gap_us = [this]() -> int {
        int bps = m_bitrateKbps.load() * 1000;
        if (bps <= 0) return 0;
        int g = static_cast<int>((static_cast<int64_t>(kRtpMtu) * 8 * 1'000'000) / bps);
        return std::min(g, 5000);  // cap at 5ms to avoid over-sleeping
    }();

    // Split H.264 bitstream into NAL units
    auto all_nals = find_nal_units(data, size);

    // ---- MUST FIX #1: SPS/PPS injection on keyframes ----
    // Extract and store SPS/PPS from keyframe NALs for late-joining clients
    if (isKeyFrame) {
        for (const auto& nal : all_nals) {
            if (nal.size == 0) continue;
            const uint8_t nal_type = nal.data[0] & 0x1F;
            if (nal_type == 7 /*SPS*/) {
                m_spsNal.assign(nal.data, nal.data + nal.size);
            } else if (nal_type == 8 /*PPS*/) {
                m_ppsNal.assign(nal.data, nal.data + nal.size);
            }
        }
    }

    // Build send queue: for keyframes prepend stored SPS+PPS before IDR
    std::vector<NalSpan> send_queue;
    if (isKeyFrame && !m_spsNal.empty() && !m_ppsNal.empty()) {
        send_queue.push_back({m_spsNal.data(), m_spsNal.size()});
        send_queue.push_back({m_ppsNal.data(), m_ppsNal.size()});
    }
    for (const auto& nal : all_nals) {
        if (nal.size == 0) continue;
        const uint8_t nal_type = nal.data[0] & 0x1F;
        // Skip SPS/PPS from frame data if we already prepended stored copies
        if (isKeyFrame && (nal_type == 7 || nal_type == 8)) continue;
        send_queue.push_back(nal);
    }

    if (send_queue.empty()) {
        spdlog::warn("[WebRtcProducer] sendH264: no NAL units in {} bytes", size);
        return;
    }

    // ---- Build all RTP packets ----
    std::vector<std::vector<uint8_t>> packets;
    const uint32_t ssrc = m_impl->rtpSsrc;

    size_t ni = 0;
    while (ni < send_queue.size()) {
        const NalSpan& nal = send_queue[ni];
        const bool last_nal = (ni == send_queue.size() - 1);

        if (nal.size > kMaxPayload) {
            // ---- FU-A fragmentation for large NALs ----
            const uint8_t nal_hdr  = nal.data[0];
            const uint8_t nri      = nal_hdr & 0x60;
            const uint8_t nal_type = nal_hdr & 0x1F;
            const uint8_t fu_ind   = nri | kH264NalFuA;

            const uint8_t* payload  = nal.data + 1;
            size_t remaining        = nal.size - 1;
            bool first_frag         = true;

            while (remaining > 0) {
                size_t chunk = std::min(remaining, kMaxPayload - kFuHeaderSize);
                remaining -= chunk;
                const bool last_frag = (remaining == 0);
                const bool marker    = last_frag && last_nal;

                uint8_t fu_hdr = nal_type;
                if (first_frag) fu_hdr |= 0x80;  // S (start) bit
                if (last_frag)  fu_hdr |= 0x40;  // E (end) bit

                std::vector<uint8_t> pkt(kRtpHeaderSize + kFuHeaderSize + chunk);
                build_rtp_header(pkt.data(), marker, m_impl->rtpSeqNum++, rtp_ts, ssrc);
                pkt[kRtpHeaderSize]     = fu_ind;
                pkt[kRtpHeaderSize + 1] = fu_hdr;
                std::memcpy(pkt.data() + kRtpHeaderSize + kFuHeaderSize, payload, chunk);
                payload += chunk;
                packets.push_back(std::move(pkt));
                first_frag = false;
            }
            ++ni;

        } else {
            // ---- MUST FIX #2: STAP-A aggregation for small NALs ----
            // Group consecutive small NALs into a single STAP-A packet
            size_t stap_end    = ni;
            size_t stap_bytes  = 1; // STAP-A type byte
            bool   can_stap    = false;

            while (stap_end < send_queue.size()) {
                const NalSpan& sn = send_queue[stap_end];
                if (sn.size > kStapSmallLimit) break;
                size_t needed = stap_bytes + 2 + sn.size;
                if (needed > kMaxPayload) break;
                stap_bytes = needed;
                ++stap_end;
                if (stap_end > ni + 1) can_stap = true;
            }

            if (can_stap) {
                // Build STAP-A packet
                const bool marker = (stap_end == send_queue.size());
                std::vector<uint8_t> pkt(kRtpHeaderSize + stap_bytes);
                build_rtp_header(pkt.data(), marker, m_impl->rtpSeqNum++, rtp_ts, ssrc);
                pkt[kRtpHeaderSize] = kH264NalStapA;
                size_t off = kRtpHeaderSize + 1;
                for (size_t k = ni; k < stap_end; ++k) {
                    const NalSpan& sn = send_queue[k];
                    pkt[off]     = static_cast<uint8_t>(sn.size >> 8);
                    pkt[off + 1] = static_cast<uint8_t>(sn.size & 0xFF);
                    std::memcpy(pkt.data() + off + 2, sn.data, sn.size);
                    off += 2 + sn.size;
                }
                packets.push_back(std::move(pkt));
                ni = stap_end;
            } else {
                // Single NAL unit packet
                const bool marker = last_nal;
                std::vector<uint8_t> pkt(kRtpHeaderSize + nal.size);
                build_rtp_header(pkt.data(), marker, m_impl->rtpSeqNum++, rtp_ts, ssrc);
                std::memcpy(pkt.data() + kRtpHeaderSize, nal.data, nal.size);
                packets.push_back(std::move(pkt));
                ++ni;
            }
        }
    }

    // ---- Send all packets with pacing ----
    for (const auto& pkt : packets) {
        try {
            send_one_rtp(m_impl->videoTrack.get(), pkt, gap_us);
        } catch (const std::exception& ex) {
            spdlog::warn("[WebRtcProducer] RTP send error: {}", ex.what());
            return;
        }
    }
}

// -----------------------------------------------------------------------
// disconnect
// -----------------------------------------------------------------------

void WebRtcProducer::disconnect()
{
    if (m_impl->videoTrack) {
        m_impl->videoTrack->close();
        m_impl->videoTrack.reset();
    }
    if (m_impl->pc) {
        m_impl->pc->close();
        m_impl->pc.reset();
    }

    if (!m_transportId.empty()) {
        m_signaling.closeTransport(m_transportId);
        m_transportId.clear();
    }

    m_producerId.clear();
    m_connected = false;
    spdlog::info("[WebRtcProducer] disconnected");
}

// -----------------------------------------------------------------------
// Transport stats
// -----------------------------------------------------------------------

WebRtcProducer::TransportStats WebRtcProducer::getStats() const
{
    std::lock_guard<std::mutex> lk(m_statsMtx);
    return m_stats;
}

void WebRtcProducer::reportStats(float packetLoss, int rttMs)
{
    std::lock_guard<std::mutex> lk(m_statsMtx);
    m_stats.packetLossFraction = packetLoss;
    m_stats.rttMs              = rttMs;
}

// -----------------------------------------------------------------------
// Reconnect with exponential backoff
// -----------------------------------------------------------------------

void WebRtcProducer::attemptReconnect()
{
    if (m_reconnectAttempts >= m_config.maxReconnectAttempts) {
        spdlog::error("[WebRtcProducer] max reconnect attempts ({}) reached",
                      m_config.maxReconnectAttempts);
        return;
    }

    ++m_reconnectAttempts;

    // Exponential backoff: baseDelay * 2^(attempt-1), capped at 60s
    constexpr int kMaxDelayMs = 60000; // milliseconds
    int delayMs = m_config.reconnectDelayMs;
    for (int i = 1; i < m_reconnectAttempts && delayMs < kMaxDelayMs; ++i) {
        delayMs = std::min(delayMs * 2, kMaxDelayMs);
    }

    spdlog::info("[WebRtcProducer] reconnecting (attempt {}/{}) in {}ms (exponential backoff)",
                 m_reconnectAttempts, m_config.maxReconnectAttempts, delayMs);

    // Schedule reconnect on a separate thread to avoid blocking the callback
    std::thread([this, delayMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        disconnect();
        connect();
    }).detach();
}

} // namespace hub32agent::webrtc

#endif // HUB32_WITH_WEBRTC
