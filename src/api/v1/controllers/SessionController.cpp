#include "core/PrecompiledHeader.hpp"
#include "SessionController.hpp"
#include "../dto/SessionDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "core/internal/PluginRegistry.hpp"

#include <httplib.h>

namespace veyon32api::api::v1 {

SessionController::SessionController(core::internal::PluginRegistry& registry)
    : m_registry(registry) {}

void SessionController::handleGetSession(const httplib::Request& /*req*/, httplib::Response& res)
{
    // TODO: sessionPlugin()->getSession(computerId) → dto::SessionDto → JSON
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

void SessionController::handleGetUser(const httplib::Request& /*req*/, httplib::Response& res)
{
    // TODO: sessionPlugin()->getUser(computerId) → dto::UserDto → JSON
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

void SessionController::handleGetScreens(const httplib::Request& /*req*/, httplib::Response& res)
{
    // TODO: sessionPlugin()->getScreens(computerId) → [dto::ScreenDto] → JSON
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

} // namespace veyon32api::api::v1
