#include "core/PrecompiledHeader.hpp"
#include "ComputerController.hpp"
#include "../dto/ComputerDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "core/internal/PluginRegistry.hpp"

#include <httplib.h>

namespace veyon32api::api::v1 {

ComputerController::ComputerController(core::internal::PluginRegistry& registry)
    : m_registry(registry)
{}

void ComputerController::handleList(const httplib::Request& /*req*/, httplib::Response& res)
{
    // TODO: m_registry.computerPlugin()->listComputers()
    // TODO: map to dto::ComputerListDto, serialize to JSON
    // TODO: support ?location= filter, ?state= filter, ?limit= ?offset=
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

void ComputerController::handleGetOne(const httplib::Request& req, httplib::Response& res)
{
    // TODO: extract :id param from req.matches
    // TODO: m_registry.computerPlugin()->getComputer(id)
    // TODO: map to dto::ComputerDto, serialize to JSON
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

void ComputerController::handleInfo(const httplib::Request& req, httplib::Response& res)
{
    // TODO: getComputer + getSession + getUser + getScreens combined
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

} // namespace veyon32api::api::v1
