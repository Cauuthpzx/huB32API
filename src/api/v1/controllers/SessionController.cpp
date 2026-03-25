#include "core/PrecompiledHeader.hpp"
#include "SessionController.hpp"
#include "../dto/SessionDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "core/internal/PluginRegistry.hpp"

#include <httplib.h>

namespace {

/**
 * @brief Sends an RFC-7807-style JSON error response.
 * @param res    The httplib response to populate.
 * @param status HTTP status code to set.
 * @param title  Short human-readable problem title.
 * @param detail Longer explanation; defaults to @p title when empty.
 */
void sendError(httplib::Response& res,
               int                status,
               const std::string& title,
               const std::string& detail = {})
{
    nlohmann::json j;
    j["status"] = status;
    j["title"]  = title;
    j["detail"] = detail.empty() ? title : detail;
    res.status  = status;
    res.set_content(j.dump(), "application/json");
}

} // anonymous namespace

namespace veyon32api::api::v1 {

/**
 * @brief Constructs the SessionController.
 * @param registry The plugin registry used to resolve the session plugin.
 */
SessionController::SessionController(core::internal::PluginRegistry& registry)
    : m_registry(registry) {}

/**
 * @brief Handles GET /api/v1/computers/:id/session — returns session info.
 *
 * The computer UID is extracted from the first regex capture group
 * (@c req.matches[1]).  Delegates to @ref SessionPluginInterface::getSession
 * and maps the result to @ref dto::SessionDto.
 *
 * Returns HTTP 200 with a SessionDto JSON body, HTTP 404 if the computer is not
 * found, or HTTP 503 on plugin error.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void SessionController::handleGetSession(const httplib::Request& req, httplib::Response& res)
{
    const std::string id = req.matches[1].str();

    auto* plugin = m_registry.sessionPlugin();
    if (!plugin) {
        sendError(res, 503, "Session plugin unavailable");
        return;
    }

    const auto result = plugin->getSession(id);
    if (result.is_err()) {
        const auto& err = result.error();
        const bool notFound = (err.code == ErrorCode::ComputerNotFound ||
                               err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, "Computer not found",
                      "No computer with id: " + id);
        } else {
            sendError(res, 503, "Failed to retrieve session", err.message);
        }
        return;
    }

    const nlohmann::json j = dto::SessionDto::from(result.value());
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles GET /api/v1/computers/:id/user — returns the logged-in user.
 *
 * The computer UID is extracted from the first regex capture group
 * (@c req.matches[1]).  Delegates to @ref SessionPluginInterface::getUser
 * and maps the result to @ref dto::UserDto.
 *
 * Returns HTTP 200 with a UserDto JSON body, HTTP 404 if the computer is not
 * found, or HTTP 503 on plugin error.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void SessionController::handleGetUser(const httplib::Request& req, httplib::Response& res)
{
    const std::string id = req.matches[1].str();

    auto* plugin = m_registry.sessionPlugin();
    if (!plugin) {
        sendError(res, 503, "Session plugin unavailable");
        return;
    }

    const auto result = plugin->getUser(id);
    if (result.is_err()) {
        const auto& err = result.error();
        const bool notFound = (err.code == ErrorCode::ComputerNotFound ||
                               err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, "Computer not found",
                      "No computer with id: " + id);
        } else {
            sendError(res, 503, "Failed to retrieve user", err.message);
        }
        return;
    }

    const nlohmann::json j = dto::UserDto::from(result.value());
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles GET /api/v1/computers/:id/screens — returns screen geometry list.
 *
 * The computer UID is extracted from the first regex capture group
 * (@c req.matches[1]).  Delegates to @ref SessionPluginInterface::getScreens
 * and builds a JSON array of objects with @c x, @c y, @c width, @c height fields.
 *
 * Returns HTTP 200 with a JSON array body, HTTP 404 if the computer is not
 * found, or HTTP 503 on plugin error.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void SessionController::handleGetScreens(const httplib::Request& req, httplib::Response& res)
{
    const std::string id = req.matches[1].str();

    auto* plugin = m_registry.sessionPlugin();
    if (!plugin) {
        sendError(res, 503, "Session plugin unavailable");
        return;
    }

    const auto result = plugin->getScreens(id);
    if (result.is_err()) {
        const auto& err = result.error();
        const bool notFound = (err.code == ErrorCode::ComputerNotFound ||
                               err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, "Computer not found",
                      "No computer with id: " + id);
        } else {
            sendError(res, 503, "Failed to retrieve screens", err.message);
        }
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const ScreenRect& rect : result.value()) {
        nlohmann::json entry;
        entry["x"]      = rect.x;
        entry["y"]      = rect.y;
        entry["width"]  = rect.width;
        entry["height"] = rect.height;
        arr.push_back(std::move(entry));
    }

    res.status = 200;
    res.set_content(arr.dump(), "application/json");
}

} // namespace veyon32api::api::v1
