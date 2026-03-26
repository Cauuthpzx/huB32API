#ifdef HUB32_WITH_WEBRTC

#include "hub32agent/webrtc/WebRtcProducer.hpp"
#include "hub32agent/webrtc/SignalingClient.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>

// libdatachannel
#include <rtc/rtc.hpp>

namespace hub32agent::webrtc {

struct WebRtcProducer::Impl
{
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> videoTrack;
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
    // mediasoup-compatible: payload type 96, H.264
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

    // TODO Phase 4.5: Complete DTLS negotiation with mediasoup SFU
    // - Set remote ICE candidates from transport response
    // - Connect transport with local DTLS parameters
    // - Create producer with video RTP parameters

    spdlog::info("[WebRtcProducer] transport created: {}", m_transportId);
    m_connected = true;
    return true;
}

void WebRtcProducer::sendH264(const uint8_t* data, size_t size,
                                int64_t timestampUs, bool isKeyFrame)
{
    if (!m_connected || !m_impl->videoTrack) return;

    // TODO Phase 4.5: RTP packetize H.264 NAL units and send via track
    // - Fragment NAL units into RTP packets (MTU ~1200 bytes)
    // - Set RTP timestamp from timestampUs (90kHz clock)
    // - Set marker bit on last packet of frame
    // - Send via m_impl->videoTrack->send()

    (void)data;
    (void)size;
    (void)timestampUs;
    (void)isKeyFrame;
}

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

void WebRtcProducer::attemptReconnect()
{
    if (m_reconnectAttempts >= m_config.maxReconnectAttempts) {
        spdlog::error("[WebRtcProducer] max reconnect attempts ({}) reached",
                      m_config.maxReconnectAttempts);
        return;
    }

    ++m_reconnectAttempts;
    spdlog::info("[WebRtcProducer] reconnecting (attempt {}/{}), delay {}ms",
                 m_reconnectAttempts, m_config.maxReconnectAttempts,
                 m_config.reconnectDelayMs);

    // Schedule reconnect on a separate thread to avoid blocking the callback
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_config.reconnectDelayMs));
        disconnect();
        connect();
    }).detach();
}

} // namespace hub32agent::webrtc

#endif // HUB32_WITH_WEBRTC
