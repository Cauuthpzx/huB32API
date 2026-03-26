#pragma once

#ifdef HUB32_WITH_WEBRTC

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <atomic>

namespace hub32agent::webrtc {

class SignalingClient;

/// @brief WebRTC video producer using libdatachannel.
/// Sends H.264 encoded frames to the mediasoup SFU via WebRTC.
class WebRtcProducer
{
public:
    /// @brief Connection state callback.
    using StateCallback = std::function<void(const std::string& state)>;

    /// @brief Configuration for the WebRTC producer.
    struct Config {
        std::string locationId;          ///< Room/location to stream into
        int reconnectDelayMs  = 3000;    ///< milliseconds -- delay between reconnect attempts
        int maxReconnectAttempts = 10;   ///< maximum reconnection attempts
    };

    WebRtcProducer(SignalingClient& signaling, const Config& config);
    ~WebRtcProducer();

    /// @brief Establishes the WebRTC connection to the SFU.
    /// Creates transport, negotiates ICE/DTLS, creates producer.
    /// @return true if connection established successfully.
    bool connect();

    /// @brief Sends an H.264 encoded frame to the SFU.
    /// @param data       H.264 NAL unit(s).
    /// @param size       Size in bytes.
    /// @param timestampUs Presentation timestamp in microseconds.
    /// @param isKeyFrame true if this is an IDR frame.
    void sendH264(const uint8_t* data, size_t size,
                   int64_t timestampUs, bool isKeyFrame);

    /// @brief Disconnects and releases WebRTC resources.
    void disconnect();

    /// @brief Returns true if connected and producing.
    bool isConnected() const { return m_connected.load(); }

    /// @brief Sets a callback for connection state changes.
    void setStateCallback(StateCallback cb) { m_stateCb = std::move(cb); }

private:
    void attemptReconnect();

    SignalingClient& m_signaling;
    Config           m_config;
    StateCallback    m_stateCb;
    std::atomic<bool> m_connected{false};
    int              m_reconnectAttempts = 0;
    std::string      m_transportId;
    std::string      m_producerId;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hub32agent::webrtc

#endif // HUB32_WITH_WEBRTC
