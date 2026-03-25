#pragma once

namespace httplib { class Request; class Response; }
namespace hub32api::core::internal { class PluginRegistry; }

namespace hub32api::api::v1 {

// -----------------------------------------------------------------------
// SessionController — handles session and user info endpoints
//
// GET /api/v1/computers/:id/session  → SessionDto
// GET /api/v1/computers/:id/user     → UserDto
// GET /api/v1/computers/:id/screens  → [ScreenDto]
// -----------------------------------------------------------------------
class SessionController
{
public:
    explicit SessionController(core::internal::PluginRegistry& registry);

    void handleGetSession(const httplib::Request& req, httplib::Response& res);
    void handleGetUser   (const httplib::Request& req, httplib::Response& res);
    void handleGetScreens(const httplib::Request& req, httplib::Response& res);

private:
    core::internal::PluginRegistry& m_registry;
};

} // namespace hub32api::api::v1
