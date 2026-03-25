#pragma once

namespace httplib { class Request; class Response; }
namespace hub32api::core::internal { class PluginRegistry; }

namespace hub32api::api::v1 {

// -----------------------------------------------------------------------
// FramebufferController — handles GET /api/v1/computers/:id/framebuffer
//
// Query params: ?width=INT &height=INT &format=png|jpeg &quality=INT
// Returns: binary image (Content-Type: image/png or image/jpeg)
// -----------------------------------------------------------------------
class FramebufferController
{
public:
    explicit FramebufferController(core::internal::PluginRegistry& registry);

    void handleGetFramebuffer(const httplib::Request& req, httplib::Response& res);

private:
    core::internal::PluginRegistry& m_registry;
};

} // namespace hub32api::api::v1
