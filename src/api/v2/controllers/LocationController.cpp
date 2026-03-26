#include "core/PrecompiledHeader.hpp"
#include "LocationController.hpp"
#include "../dto/LocationDto.hpp"
#include "db/LocationRepository.hpp"
#include "db/ComputerRepository.hpp"
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
 * @param locationRepo  Repository for location records.
 * @param computerRepo  Repository for computer records (used for counts/IDs).
 */
LocationController::LocationController(db::LocationRepository& locationRepo,
                                       db::ComputerRepository& computerRepo)
    : m_locationRepo(locationRepo)
    , m_computerRepo(computerRepo)
{}

/**
 * @brief Handles GET /api/v2/locations.
 *
 * Retrieves all locations from the LocationRepository, and for each location
 * queries the ComputerRepository to get computer IDs and counts.
 *
 * @param req  Incoming HTTP request.
 * @param res  Outgoing HTTP response.
 */
void LocationController::handleList(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    auto result = m_locationRepo.listAll();
    if (result.is_err()) {
        sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"), result.error().message);
        return;
    }

    const auto& locations = result.value();

    dto::LocationListDto listDto;
    listDto.locations.reserve(locations.size());

    for (const auto& loc : locations) {
        dto::LocationDto locDto;
        locDto.id   = loc.id;
        locDto.name = loc.name;

        // Get computers belonging to this location
        auto computersResult = m_computerRepo.listByLocation(loc.id);
        if (computersResult.is_ok()) {
            const auto& computers = computersResult.value();
            locDto.computerCount = static_cast<int>(computers.size());
            locDto.computerIds.reserve(computers.size());
            for (const auto& comp : computers) {
                locDto.computerIds.push_back(comp.id);
            }
        }

        listDto.locations.push_back(std::move(locDto));
    }

    listDto.total = static_cast<int>(listDto.locations.size());

    res.status = 200;
    res.set_content(nlohmann::json(listDto).dump(), "application/json");
}

/**
 * @brief Handles GET /api/v2/locations/:id.
 *
 * Looks up the location by ID from the LocationRepository, then queries
 * the ComputerRepository for computers belonging to that location.
 *
 * Returns HTTP 404 if the location is not found, HTTP 503 on repository error.
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

    auto locResult = m_locationRepo.findById(locationId);
    if (locResult.is_err()) {
        const auto& err = locResult.error();
        const bool notFound = (err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, tr(lang, "error.computer_not_found"));
        } else {
            sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"), err.message);
        }
        return;
    }

    const auto& loc = locResult.value();

    dto::LocationDto locDto;
    locDto.id   = loc.id;
    locDto.name = loc.name;

    // Get computers belonging to this location
    auto computersResult = m_computerRepo.listByLocation(locationId);
    if (computersResult.is_ok()) {
        const auto& computers = computersResult.value();
        locDto.computerCount = static_cast<int>(computers.size());
        locDto.computerIds.reserve(computers.size());
        for (const auto& comp : computers) {
            locDto.computerIds.push_back(comp.id);
        }
    }

    res.status = 200;
    res.set_content(nlohmann::json(locDto).dump(), "application/json");
}

} // namespace hub32api::api::v2
