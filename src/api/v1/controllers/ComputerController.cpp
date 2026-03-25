#include "core/PrecompiledHeader.hpp"
#include "ComputerController.hpp"
#include "../dto/ComputerDto.hpp"
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
 * @brief Constructs the ComputerController.
 * @param registry The plugin registry used to resolve the computer plugin.
 */
ComputerController::ComputerController(core::internal::PluginRegistry& registry)
    : m_registry(registry)
{}

/**
 * @brief Handles GET /api/v1/computers — returns a filtered list of computers.
 *
 * Supported query parameters:
 *   - @c location — case-sensitive substring match on the computer's location field.
 *   - @c state    — exact match on the computer's state string (e.g. "online").
 *
 * Returns HTTP 200 with a @ref dto::ComputerListDto JSON body, or HTTP 503 if
 * the computer plugin is unavailable or returns an error.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void ComputerController::handleList(const httplib::Request& req, httplib::Response& res)
{
    auto* plugin = m_registry.computerPlugin();
    if (!plugin) {
        sendError(res, 503, "Computer plugin unavailable");
        return;
    }

    const auto result = plugin->listComputers();
    if (result.is_err()) {
        sendError(res, 503, "Failed to list computers",
                  result.error().message);
        return;
    }

    // Read optional filter params (empty string means "no filter")
    const std::string locationFilter = req.get_param_value("location");
    const std::string stateFilter    = req.get_param_value("state");

    dto::ComputerListDto listDto;
    for (const auto& info : result.value()) {
        const auto dto = dto::ComputerDto::from(info);

        // Apply location filter
        if (!locationFilter.empty() && dto.location != locationFilter) {
            continue;
        }
        // Apply state filter
        if (!stateFilter.empty() && dto.state != stateFilter) {
            continue;
        }

        listDto.computers.push_back(dto);
    }
    listDto.total = static_cast<int>(listDto.computers.size());

    const nlohmann::json j = listDto;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles GET /api/v1/computers/:id — returns a single computer by UID.
 *
 * The computer UID is extracted from the first capture group of the route regex
 * (i.e. @c req.matches[1]).  Returns HTTP 200 with a @ref dto::ComputerDto JSON
 * body, HTTP 404 if the computer is not found, or HTTP 503 on plugin error.
 *
 * @param req  The incoming HTTP request (must have at least one regex match).
 * @param res  The HTTP response to populate.
 */
void ComputerController::handleGetOne(const httplib::Request& req, httplib::Response& res)
{
    const std::string id = req.matches[1].str();

    auto* plugin = m_registry.computerPlugin();
    if (!plugin) {
        sendError(res, 503, "Computer plugin unavailable");
        return;
    }

    const auto result = plugin->getComputer(id);
    if (result.is_err()) {
        const auto& err = result.error();
        const bool notFound = (err.code == ErrorCode::ComputerNotFound ||
                               err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, "Computer not found",
                      "No computer with id: " + id);
        } else {
            sendError(res, 503, "Failed to retrieve computer", err.message);
        }
        return;
    }

    const nlohmann::json j = dto::ComputerDto::from(result.value());
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles GET /api/v1/computers/:id/info — returns combined computer + state.
 *
 * Retrieves the computer record and its current connection state, then returns
 * a combined JSON object:
 * @code
 * { "computer": { ...ComputerDto... }, "state": "online" }
 * @endcode
 *
 * Returns HTTP 404 if the computer is not found, HTTP 503 on plugin error.
 *
 * @param req  The incoming HTTP request (must have at least one regex match).
 * @param res  The HTTP response to populate.
 */
void ComputerController::handleInfo(const httplib::Request& req, httplib::Response& res)
{
    const std::string id = req.matches[1].str();

    auto* plugin = m_registry.computerPlugin();
    if (!plugin) {
        sendError(res, 503, "Computer plugin unavailable");
        return;
    }

    // Retrieve computer info
    const auto compResult = plugin->getComputer(id);
    if (compResult.is_err()) {
        const auto& err = compResult.error();
        const bool notFound = (err.code == ErrorCode::ComputerNotFound ||
                               err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, "Computer not found",
                      "No computer with id: " + id);
        } else {
            sendError(res, 503, "Failed to retrieve computer", err.message);
        }
        return;
    }

    // Retrieve current state (best-effort; fall back to info.state on error)
    std::string stateStr;
    const auto stateResult = plugin->getState(id);
    if (stateResult.is_ok()) {
        switch (stateResult.value()) {
            case ComputerState::Online:        stateStr = "online";        break;
            case ComputerState::Offline:       stateStr = "offline";       break;
            case ComputerState::Connected:     stateStr = "connected";     break;
            case ComputerState::Connecting:    stateStr = "connecting";    break;
            case ComputerState::Disconnecting: stateStr = "disconnecting"; break;
            default:                           stateStr = "unknown";       break;
        }
    } else {
        // Fall back to the state embedded in ComputerInfo
        stateStr = dto::ComputerDto::from(compResult.value()).state;
    }

    nlohmann::json j;
    j["computer"] = dto::ComputerDto::from(compResult.value());
    j["state"]    = stateStr;

    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

} // namespace veyon32api::api::v1
