#pragma once

namespace httplib { class Request; class Response; }
namespace veyon32api::core::internal { class PluginRegistry; }

namespace veyon32api::api::v2 {

// -----------------------------------------------------------------------
// BatchController — v2 exclusive batch operations
//
// POST /api/v2/batch/features   → apply feature to multiple computers
// GET  /api/v2/batch/:jobId     → async job status (future)
// -----------------------------------------------------------------------
class BatchController
{
public:
    explicit BatchController(core::internal::PluginRegistry& registry);

    void handleBatchFeature(const httplib::Request& req, httplib::Response& res);

private:
    core::internal::PluginRegistry& m_registry;
};

} // namespace veyon32api::api::v2
