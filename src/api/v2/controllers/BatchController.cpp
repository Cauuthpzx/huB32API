#include "core/PrecompiledHeader.hpp"
#include "BatchController.hpp"
#include "../dto/BatchDto.hpp"
#include "core/internal/PluginRegistry.hpp"
#include <httplib.h>

namespace veyon32api::api::v2 {

BatchController::BatchController(core::internal::PluginRegistry& registry)
    : m_registry(registry) {}

void BatchController::handleBatchFeature(
    const httplib::Request& /*req*/, httplib::Response& res)
{
    // TODO: parse dto::BatchFeatureRequest from req.body
    // TODO: call featurePlugin()->controlFeatureBatch(ids, featureUid, op, args)
    // TODO: collect results → dto::BatchFeatureResponse → JSON 200
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

} // namespace veyon32api::api::v2
