#include "core/PrecompiledHeader.hpp"
#include "LocationController.hpp"
#include "../dto/LocationDto.hpp"
#include "core/internal/PluginRegistry.hpp"
#include <httplib.h>

namespace veyon32api::api::v2 {

LocationController::LocationController(core::internal::PluginRegistry& registry)
    : m_registry(registry) {}

void LocationController::handleList(const httplib::Request&, httplib::Response& res)
{
    // TODO: computerPlugin()->listComputers() → group by location → LocationListDto
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

void LocationController::handleGetOne(const httplib::Request&, httplib::Response& res)
{
    // TODO: filter computers by location id
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

} // namespace veyon32api::api::v2
