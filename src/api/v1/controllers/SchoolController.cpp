#include "core/PrecompiledHeader.hpp"
#include "SchoolController.hpp"
#include "../dto/SchoolDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "db/SchoolRepository.hpp"
#include "db/LocationRepository.hpp"
#include "db/ComputerRepository.hpp"
#include "auth/JwtAuth.hpp"
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

/**
 * @brief Extracts the Bearer token from the Authorization header and checks
 *        that the caller holds the "admin", "superadmin", or "owner" role.
 * @return true if authorized; false if 403 has been sent.
 */
bool requireAdmin(const httplib::Request& req, httplib::Response& res,
                  hub32api::auth::JwtAuth& jwtAuth, const std::string& lang)
{
    using hub32api::core::internal::tr;
    const std::string authHeader = req.get_header_value("Authorization");
    if (authHeader.size() <= hub32api::kBearerPrefixLen) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return false;
    }
    const std::string token = authHeader.substr(hub32api::kBearerPrefixLen);
    auto result = jwtAuth.authenticate(token);
    if (result.is_err() || !result.value().token) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return false;
    }
    const auto& role = result.value().token->role;
    if (role != "admin" && role != "superadmin" && role != "owner") {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return false;
    }
    return true;
}

/**
 * @brief Extracts the tenant_id claim from the Bearer token in the Authorization header.
 *
 * @return The tenant_id string, or an empty string if the token is absent/invalid.
 */
std::string getTenantId(const httplib::Request& req, hub32api::auth::JwtAuth& jwtAuth)
{
    const std::string authHeader = req.get_header_value("Authorization");
    if (authHeader.size() <= hub32api::kBearerPrefixLen) return {};
    const std::string token = authHeader.substr(hub32api::kBearerPrefixLen);
    auto result = jwtAuth.authenticate(token);
    if (result.is_err() || !result.value().token) return {};
    return result.value().token->tenant_id;
}

} // anonymous namespace

namespace hub32api::api::v1 {

SchoolController::SchoolController(
    db::SchoolRepository& schoolRepo,
    db::LocationRepository& locationRepo,
    db::ComputerRepository& computerRepo,
    auth::JwtAuth& jwtAuth)
    : m_schoolRepo(schoolRepo)
    , m_locationRepo(locationRepo)
    , m_computerRepo(computerRepo)
    , m_jwtAuth(jwtAuth)
{}

// ── Schools ──────────────────────────────────────────────────────────────────

void SchoolController::handleCreateSchool(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    dto::CreateSchoolRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::CreateSchoolRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.name.empty()) {
        sendError(res, 400, tr(lang, "error.name_required"));
        return;
    }

    auto result = m_schoolRepo.create(dto.name, dto.address);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    // Fetch the created record
    auto fetched = m_schoolRepo.findById(result.value());
    if (fetched.is_err()) {
        sendError(res, 500, fetched.error().message);
        return;
    }

    const auto& rec = fetched.value();
    dto::SchoolResponse resp;
    resp.id        = rec.id;
    resp.name      = rec.name;
    resp.address   = rec.address;
    resp.createdAt = rec.createdAt;

    const nlohmann::json j = resp;
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

void SchoolController::handleListSchools(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    const std::string tenantId = getTenantId(req, m_jwtAuth);

    // superadmin (no tenantId) sees all schools; owner sees only their own tenant's schools.
    auto result = tenantId.empty()
        ? m_schoolRepo.listAll()
        : m_schoolRepo.listByTenant(tenantId);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& rec : result.value()) {
        dto::SchoolResponse resp;
        resp.id        = rec.id;
        resp.name      = rec.name;
        resp.address   = rec.address;
        resp.createdAt = rec.createdAt;
        arr.push_back(nlohmann::json(resp));
    }

    nlohmann::json j;
    j["schools"] = arr;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void SchoolController::handleGetSchool(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();
    auto result = m_schoolRepo.findById(id);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.school_not_found"));
        return;
    }

    const auto& rec = result.value();
    dto::SchoolResponse resp;
    resp.id        = rec.id;
    resp.name      = rec.name;
    resp.address   = rec.address;
    resp.createdAt = rec.createdAt;

    const nlohmann::json j = resp;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void SchoolController::handleUpdateSchool(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    dto::CreateSchoolRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::CreateSchoolRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.name.empty()) {
        sendError(res, 400, tr(lang, "error.name_required"));
        return;
    }

    auto updateResult = m_schoolRepo.update(id, dto.name, dto.address);
    if (updateResult.is_err()) {
        sendError(res, 404, tr(lang, "error.school_not_found"));
        return;
    }

    // Return the updated record
    auto fetched = m_schoolRepo.findById(id);
    if (fetched.is_err()) {
        sendError(res, 500, fetched.error().message);
        return;
    }

    const auto& rec = fetched.value();
    dto::SchoolResponse resp;
    resp.id        = rec.id;
    resp.name      = rec.name;
    resp.address   = rec.address;
    resp.createdAt = rec.createdAt;

    const nlohmann::json j = resp;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void SchoolController::handleDeleteSchool(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();
    auto result = m_schoolRepo.remove(id);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.school_not_found"));
        return;
    }

    res.status = 204;
}

// ── Locations ────────────────────────────────────────────────────────────────

void SchoolController::handleCreateLocation(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    dto::CreateLocationRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::CreateLocationRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.name.empty()) {
        sendError(res, 400, tr(lang, "error.name_required"));
        return;
    }

    auto result = m_locationRepo.create(dto.schoolId, dto.name, dto.building,
                                         dto.floor, dto.capacity, dto.type);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    auto fetched = m_locationRepo.findById(result.value());
    if (fetched.is_err()) {
        sendError(res, 500, fetched.error().message);
        return;
    }

    const auto& rec = fetched.value();
    dto::LocationResponse resp;
    resp.id       = rec.id;
    resp.schoolId = rec.schoolId;
    resp.name     = rec.name;
    resp.building = rec.building;
    resp.floor    = rec.floor;
    resp.capacity = rec.capacity;
    resp.type     = rec.type;

    const nlohmann::json j = resp;
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

void SchoolController::handleListLocations(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // List locations — protected but not admin-only
    auto result = m_locationRepo.listAll();
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& rec : result.value()) {
        dto::LocationResponse resp;
        resp.id       = rec.id;
        resp.schoolId = rec.schoolId;
        resp.name     = rec.name;
        resp.building = rec.building;
        resp.floor    = rec.floor;
        resp.capacity = rec.capacity;
        resp.type     = rec.type;
        arr.push_back(nlohmann::json(resp));
    }

    nlohmann::json j;
    j["locations"] = arr;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void SchoolController::handleGetLocation(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // Get single location — protected but not admin-only
    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();
    auto result = m_locationRepo.findById(id);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.location_not_found"));
        return;
    }

    const auto& rec = result.value();
    dto::LocationResponse resp;
    resp.id       = rec.id;
    resp.schoolId = rec.schoolId;
    resp.name     = rec.name;
    resp.building = rec.building;
    resp.floor    = rec.floor;
    resp.capacity = rec.capacity;
    resp.type     = rec.type;

    const nlohmann::json j = resp;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void SchoolController::handleUpdateLocation(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    dto::CreateLocationRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::CreateLocationRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.name.empty()) {
        sendError(res, 400, tr(lang, "error.name_required"));
        return;
    }

    auto updateResult = m_locationRepo.update(id, dto.name, dto.building,
                                               dto.floor, dto.capacity, dto.type);
    if (updateResult.is_err()) {
        sendError(res, 404, tr(lang, "error.location_not_found"));
        return;
    }

    auto fetched = m_locationRepo.findById(id);
    if (fetched.is_err()) {
        sendError(res, 500, fetched.error().message);
        return;
    }

    const auto& rec = fetched.value();
    dto::LocationResponse resp;
    resp.id       = rec.id;
    resp.schoolId = rec.schoolId;
    resp.name     = rec.name;
    resp.building = rec.building;
    resp.floor    = rec.floor;
    resp.capacity = rec.capacity;
    resp.type     = rec.type;

    const nlohmann::json j = resp;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void SchoolController::handleDeleteLocation(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();
    auto result = m_locationRepo.remove(id);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.location_not_found"));
        return;
    }

    res.status = 204;
}

void SchoolController::handleListLocationComputers(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // Protected but not admin-only
    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string locationId = req.matches[1].str();

    // Verify location exists
    auto locResult = m_locationRepo.findById(locationId);
    if (locResult.is_err()) {
        sendError(res, 404, tr(lang, "error.location_not_found"));
        return;
    }

    auto result = m_computerRepo.listByLocation(locationId);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& rec : result.value()) {
        nlohmann::json c;
        c["id"]            = rec.id;
        c["locationId"]    = rec.locationId;
        c["hostname"]      = rec.hostname;
        c["macAddress"]    = rec.macAddress;
        c["ipLastSeen"]    = rec.ipLastSeen;
        c["agentVersion"]  = rec.agentVersion;
        c["lastHeartbeat"] = rec.lastHeartbeat;
        c["state"]         = rec.state;
        c["positionX"]     = rec.positionX;
        c["positionY"]     = rec.positionY;
        arr.push_back(c);
    }

    nlohmann::json j;
    j["computers"] = arr;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

} // namespace hub32api::api::v1
