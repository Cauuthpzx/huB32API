#include "core/PrecompiledHeader.hpp"
#include "FeatureController.hpp"
#include "../dto/FeatureDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "core/internal/PluginRegistry.hpp"

#include <httplib.h>

namespace veyon32api::api::v1 {

FeatureController::FeatureController(core::internal::PluginRegistry& registry)
    : m_registry(registry)
{}

void FeatureController::handleList(const httplib::Request& /*req*/, httplib::Response& res)
{
    // TODO: extract computer id from req, call featurePlugin()->listFeatures(id)
    // TODO: serialize dto::FeatureListDto to JSON
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

void FeatureController::handleGetOne(const httplib::Request& /*req*/, httplib::Response& res)
{
    // TODO: extract computer id + feature uid, call isFeatureActive()
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

void FeatureController::handleControl(const httplib::Request& req, httplib::Response& res)
{
    // TODO: parse dto::FeatureControlRequest from req.body
    // TODO: map active=true → FeatureOperation::Start, active=false → Stop
    // TODO: call featurePlugin()->controlFeature(computerId, featureId, op, args)
    // TODO: return 200 on success
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

} // namespace veyon32api::api::v1
