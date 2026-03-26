#pragma once
#include <string>

namespace httplib { class Request; class Response; }
namespace hub32api::media { class SfuBackend; class RoomManager; }

namespace hub32api::api::v1 {

class StreamController
{
public:
    StreamController(media::RoomManager& roomManager, media::SfuBackend& backend,
                     const std::string& turnSecret, const std::string& turnServerUrl);

    // POST /api/v1/stream/transport — create WebRTC transport
    void handleCreateTransport(const httplib::Request& req, httplib::Response& res);

    // POST /api/v1/stream/transport/:id/connect — DTLS connect
    void handleConnectTransport(const httplib::Request& req, httplib::Response& res);

    // POST /api/v1/stream/produce — create Producer
    void handleProduce(const httplib::Request& req, httplib::Response& res);

    // POST /api/v1/stream/consume — create Consumer
    void handleConsume(const httplib::Request& req, httplib::Response& res);

    // DELETE /api/v1/stream/transport/:id — close transport
    void handleCloseTransport(const httplib::Request& req, httplib::Response& res);

    // GET /api/v1/stream/ice-servers — return STUN+TURN URLs with credentials
    void handleGetIceServers(const httplib::Request& req, httplib::Response& res);

    // GET /api/v1/stream/capabilities/:locationId — Router RTP capabilities
    void handleGetCapabilities(const httplib::Request& req, httplib::Response& res);

private:
    media::RoomManager& m_roomManager;
    media::SfuBackend& m_backend;
    std::string m_turnSecret;
    std::string m_turnServerUrl;
};

} // namespace hub32api::api::v1
