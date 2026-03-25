#include "core/PrecompiledHeader.hpp"
#include "FramebufferController.hpp"
#include "core/internal/PluginRegistry.hpp"

#include <httplib.h>

namespace veyon32api::api::v1 {

FramebufferController::FramebufferController(core::internal::PluginRegistry& registry)
    : m_registry(registry)
{}

void FramebufferController::handleGetFramebuffer(
    const httplib::Request& req, httplib::Response& res)
{
    // TODO: extract :id from req.matches
    // TODO: parse ?width, ?height, ?format, ?quality query params
    // TODO: call computerPlugin()->getFramebuffer(id, w, h, fmt)
    // TODO: set res.body to raw image bytes
    // TODO: set Content-Type: image/png or image/jpeg
    // TODO: set Cache-Control and ETag headers
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

} // namespace veyon32api::api::v1
