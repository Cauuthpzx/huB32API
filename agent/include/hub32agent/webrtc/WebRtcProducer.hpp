#pragma once

#ifdef HUB32_WITH_WEBRTC

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
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

    /// @brief Updates the target bitrate used for RTP pacing.
    void setPacingBitrate(int kbps) { m_bitrateKbps.store(kbps); }

    /// @brief Transport statistics snapshot.
    struct TransportStats {
        float packetLossFraction = 0.f;  ///< [0.0, 1.0]
        int   rttMs              = 0;    ///< round-trip time in milliseconds
    };

    /// @brief Returns the last known transport stats. Thread-safe.
    TransportStats getStats() const;

    /// @brief Updates transport stats from external source. Thread-safe.
    void reportStats(float packetLoss, int rttMs);

private:
    /// @brief Schedules a reconnect with exponential backoff.
    /// Re-creates transport+producer on reconnect. Does NOT re-probe encoders.
    void attemptReconnect();

    SignalingClient& m_signaling;
    Config           m_config;
    StateCallback    m_stateCb;
    std::atomic<bool> m_connected{false};
    int              m_reconnectAttempts = 0;
    std::string      m_transportId;
    std::string      m_producerId;

    // RTP pacing bitrate (updated by StreamPipeline on quality change)
    std::atomic<int> m_bitrateKbps{2000};

    // Reconnect guard — prevents multiple concurrent reconnect threads and
    // allows cancellation when disconnect() is called during a pending reconnect.
    std::atomic<bool> m_reconnecting{false};

    // SPS/PPS NAL storage for keyframe injection (send thread only — no lock needed)
    std::vector<uint8_t> m_spsNal;
    std::vector<uint8_t> m_ppsNal;

    // Transport stats (written from external thread, read from quality loop)
    mutable std::mutex m_statsMtx;
    TransportStats     m_stats;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hub32agent::webrtc

#endif // HUB32_WITH_WEBRTC
