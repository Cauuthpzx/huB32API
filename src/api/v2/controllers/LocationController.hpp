#pragma once

namespace httplib { class Request; class Response; }
namespace veyon32api::core::internal { class PluginRegistry; }

namespace veyon32api::api::v2 {

// GET /api/v2/locations       → LocationListDto
// GET /api/v2/locations/:id   → LocationDto with computerIds
class LocationController
{
public:
    explicit LocationController(core::internal::PluginRegistry& registry);

    void handleList  (const httplib::Request& req, httplib::Response& res);
    void handleGetOne(const httplib::Request& req, httplib::Response& res);

private:
    core::internal::PluginRegistry& m_registry;
};

} // namespace veyon32api::api::v2
