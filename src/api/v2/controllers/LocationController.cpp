#include "core/PrecompiledHeader.hpp"
#include "LocationController.hpp"
#include "../dto/LocationDto.hpp"
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

namespace hub32api::api::v2 {

/**
 * @brief Constructs a LocationController.
 *
 * @param registry  Plugin registry used to obtain the computer plugin.
 */
LocationController::LocationController(core::internal::PluginRegistry& registry)
    : m_registry(registry) {}

/**
 * @brief Handles GET /api/v2/locations.
 *
 * Retrieves all known computers via the computer plugin, groups them by their
 * `location` field, and returns a `dto::LocationListDto` serialised as JSON
 * with HTTP 200.
 *
 * If the computer plugin returns an error, an RFC 7807-compatible JSON error
 * body is returned with HTTP 503.
 *
 * @param req  Incoming HTTP request (unused).
 * @param res  Outgoing HTTP response.
 */
void LocationController::handleList(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    auto* computerPlugin = m_registry.computerPlugin();
    if (!computerPlugin) {
        sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"));
        return;
    }

    auto result = computerPlugin->listComputers();
    if (result.is_err()) {
        sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"), result.error().message);
        return;
    }

    const auto& computers = result.value();

    // -----------------------------------------------------------------------
    // Group computer UIDs by location name, preserving insertion order via
    // std::map so the output is deterministic.
    // -----------------------------------------------------------------------
    std::map<std::string, std::vector<std::string>> locationMap;
    for (const auto& info : computers) {
        const std::string& loc = info.location.empty() ? "(unassigned)" : info.location;
        locationMap[loc].push_back(info.uid);
    }

    // -----------------------------------------------------------------------
    // Build the response DTO
    // -----------------------------------------------------------------------
    dto::LocationListDto listDto;
    listDto.locations.reserve(locationMap.size());

    for (const auto& [locName, ids] : locationMap) {
        dto::LocationDto locDto;
        locDto.id            = locName;
        locDto.name          = locName;
        locDto.computerIds   = ids;
        locDto.computerCount = static_cast<int>(ids.size());
        listDto.locations.push_back(std::move(locDto));
    }

    listDto.total = static_cast<int>(listDto.locations.size());

    res.status = 200;
    res.set_content(nlohmann::json(listDto).dump(), "application/json");
}

/**
 * @brief Handles GET /api/v2/locations/:id.
 *
 * Looks up the location identifier captured in `req.matches[1]`, filters
 * the full computer list to those whose `location` field matches, and returns
 * a single `dto::LocationDto` as JSON with HTTP 200.
 *
 * Returns HTTP 404 if no computers belong to the requested location, and
 * HTTP 503 if the computer plugin is unavailable or returns an error.
 *
 * @param req  Incoming HTTP request; `req.matches[1]` must hold the
 *             URL-captured location identifier.
 * @param res  Outgoing HTTP response.
 */
void LocationController::handleGetOne(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // Extract the location id captured by the route regex
    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string locationId = req.matches[1].str();

    auto* computerPlugin = m_registry.computerPlugin();
    if (!computerPlugin) {
        sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"));
        return;
    }

    auto result = computerPlugin->listComputers();
    if (result.is_err()) {
        sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"), result.error().message);
        return;
    }

    // -----------------------------------------------------------------------
    // Filter computers that belong to the requested location
    // -----------------------------------------------------------------------
    const auto& computers = result.value();
    std::vector<std::string> matchedIds;

    for (const auto& info : computers) {
        const std::string& loc = info.location.empty() ? "(unassigned)" : info.location;
        if (loc == locationId) {
            matchedIds.push_back(info.uid);
        }
    }

    if (matchedIds.empty()) {
        sendError(res, 404, tr(lang, "error.computer_not_found"));
        return;
    }

    dto::LocationDto locDto;
    locDto.id            = locationId;
    locDto.name          = locationId;
    locDto.computerIds   = std::move(matchedIds);
    locDto.computerCount = static_cast<int>(locDto.computerIds.size());

    res.status = 200;
    res.set_content(nlohmann::json(locDto).dump(), "application/json");
}

} // namespace hub32api::api::v2
