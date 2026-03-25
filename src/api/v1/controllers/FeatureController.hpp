#pragma once

namespace httplib { class Request; class Response; }
namespace veyon32api::core::internal { class PluginRegistry; }

namespace veyon32api::api::v1 {

// -----------------------------------------------------------------------
// FeatureController — handles /api/v1/computers/:id/features
//
// GET  /api/v1/computers/:id/features          → list features + active state
// GET  /api/v1/computers/:id/features/:fid     → single feature state
// PUT  /api/v1/computers/:id/features/:fid     → start/stop feature
// -----------------------------------------------------------------------
class FeatureController
{
public:
    explicit FeatureController(core::internal::PluginRegistry& registry);

    void handleList      (const httplib::Request& req, httplib::Response& res);
    void handleGetOne    (const httplib::Request& req, httplib::Response& res);
    void handleControl   (const httplib::Request& req, httplib::Response& res);

private:
    core::internal::PluginRegistry& m_registry;
};

} // namespace veyon32api::api::v1
