#include "core/PrecompiledHeader.hpp"
#include "ComputerController.hpp"
#include "../dto/ComputerDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "core/internal/PluginRegistry.hpp"
#include "core/internal/I18n.hpp"
#include "api/common/HttpErrorUtil.hpp"

#include <httplib.h>

using hub32api::api::common::sendError;

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

} // anonymous namespace

namespace hub32api::api::v1 {

/**
 * @brief Constructs the ComputerController.
 * @param registry The plugin registry used to resolve the computer plugin.
 */
ComputerController::ComputerController(core::internal::PluginRegistry& registry)
    : m_registry(registry)
{}

/**
 * @brief Handles GET /api/v1/computers — filtered, cursor-paginated computer list.
 *
 * Supported query parameters:
 *   - @c location — exact match on the computer's location field.
 *   - @c state    — exact match on the computer's state string (e.g. "online").
 *   - @c limit    — maximum items per page (default 50, max 200).
 *   - @c after    — opaque cursor (base64-encoded UID of last item seen).
 *                   Omit for the first page.
 *
 * Response body:
 * @code
 * {
 *   "computers": [ ...ComputerDto... ],
 *   "page": { "total": N, "limit": L, "nextCursor": "..." | null }
 * }
 * @endcode
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void ComputerController::handleList(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    auto* plugin = m_registry.computerPlugin();
    if (!plugin) {
        sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"));
        return;
    }

    const auto result = plugin->listComputers();
    if (result.is_err()) {
        sendError(res, 503, tr(lang, "error.failed_list_computers"), result.error().message);
        return;
    }

    // ── Parse query parameters ────────────────────────────────────────────
    const std::string locationFilter = req.get_param_value("location");
    const std::string stateFilter    = req.get_param_value("state");
    const std::string afterCursor    = req.get_param_value("after");

    int limit = kDefaultPageSize;
    if (req.has_param("limit")) {
        try {
            limit = std::stoi(req.get_param_value("limit"));
            limit = std::clamp(limit, 1, kMaxPageSize);
        } catch (...) { /* use default */ }
    }

    // ── Filter all matching computers ─────────────────────────────────────
    std::vector<dto::ComputerDto> filtered;
    filtered.reserve(result.value().size());
    for (const auto& info : result.value()) {
        const auto d = dto::ComputerDto::from(info);
        if (!locationFilter.empty() && d.location != locationFilter) continue;
        if (!stateFilter.empty()    && d.state    != stateFilter)    continue;
        filtered.push_back(d);
    }

    const int total = static_cast<int>(filtered.size());

    // ── Apply cursor (find start index after the cursor UID) ──────────────
    int startIdx = 0;
    if (!afterCursor.empty()) {
        // Cursor is the plain UID of the last-seen item (not encoded for simplicity)
        for (int i = 0; i < static_cast<int>(filtered.size()); ++i) {
            if (filtered[i].id == afterCursor) {
                startIdx = i + 1;
                break;
            }
        }
    }

    // ── Extract page ──────────────────────────────────────────────────────
    const int endIdx = std::min(startIdx + limit, static_cast<int>(filtered.size()));
    std::vector<dto::ComputerDto> page(
        filtered.begin() + startIdx,
        filtered.begin() + endIdx);

    // nextCursor is the UID of the last item on this page, or null if exhausted
    nlohmann::json nextCursor = nullptr;
    if (endIdx < static_cast<int>(filtered.size())) {
        nextCursor = page.back().id;
    }

    // ── Build response ────────────────────────────────────────────────────
    nlohmann::json j;
    j["computers"] = page;
    j["page"] = {
        {"total",      total},
        {"limit",      limit},
        {"nextCursor", nextCursor}
    };

    res.status = 200;
    res.set_header("X-Total-Count", std::to_string(total));
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
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto* plugin = m_registry.computerPlugin();
    if (!plugin) {
        sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"));
        return;
    }

    const auto result = plugin->getComputer(id);
    if (result.is_err()) {
        const auto& err = result.error();
        const bool notFound = (err.code == ErrorCode::ComputerNotFound ||
                               err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, tr(lang, "error.computer_not_found"),
                      tr(lang, "error.no_computer_with_id", {id}));
        } else {
            sendError(res, 503, tr(lang, "error.failed_retrieve_computer"), err.message);
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
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto* plugin = m_registry.computerPlugin();
    if (!plugin) {
        sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"));
        return;
    }

    // Retrieve computer info
    const auto compResult = plugin->getComputer(id);
    if (compResult.is_err()) {
        const auto& err = compResult.error();
        const bool notFound = (err.code == ErrorCode::ComputerNotFound ||
                               err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, tr(lang, "error.computer_not_found"),
                      tr(lang, "error.no_computer_with_id", {id}));
        } else {
            sendError(res, 503, tr(lang, "error.failed_retrieve_computer"), err.message);
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

} // namespace hub32api::api::v1
