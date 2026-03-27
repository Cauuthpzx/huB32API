#include "core/PrecompiledHeader.hpp"
#include "TeacherController.hpp"
#include "../dto/TeacherDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "db/TeacherRepository.hpp"
#include "db/TeacherLocationRepository.hpp"
#include "db/LocationRepository.hpp"
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
 * @brief Checks that the caller holds the "admin", "superadmin", or "owner" role.
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

TeacherController::TeacherController(
    db::TeacherRepository& teacherRepo,
    db::TeacherLocationRepository& teacherLocationRepo,
    db::LocationRepository& locationRepo,
    auth::JwtAuth& jwtAuth)
    : m_teacherRepo(teacherRepo)
    , m_teacherLocationRepo(teacherLocationRepo)
    , m_locationRepo(locationRepo)
    , m_jwtAuth(jwtAuth)
{}

void TeacherController::handleCreateTeacher(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    dto::CreateTeacherRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::CreateTeacherRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.username.empty()) {
        sendError(res, 400, tr(lang, "error.username_required"));
        return;
    }
    if (dto.password.empty()) {
        sendError(res, 400, tr(lang, "error.password_required"));
        return;
    }

    // Check if username already exists
    auto existing = m_teacherRepo.findByUsername(dto.username);
    if (existing.is_ok()) {
        sendError(res, 409, tr(lang, "error.teacher_exists"));
        return;
    }

    auto result = m_teacherRepo.create(dto.username, dto.password, dto.fullName, dto.role);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    auto fetched = m_teacherRepo.findById(result.value());
    if (fetched.is_err()) {
        sendError(res, 500, fetched.error().message);
        return;
    }

    const auto& rec = fetched.value();
    dto::TeacherResponse resp;
    resp.id        = rec.id;
    resp.username  = rec.username;
    resp.fullName  = rec.fullName;
    resp.role      = rec.role;
    resp.createdAt = rec.createdAt;

    const nlohmann::json j = resp;
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

void TeacherController::handleListTeachers(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    const std::string tenantId = getTenantId(req, m_jwtAuth);

    // superadmin (no tenantId) sees all teachers; owner sees only their own tenant's teachers.
    auto result = m_teacherRepo.listAll(tenantId);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& rec : result.value()) {
        dto::TeacherResponse resp;
        resp.id        = rec.id;
        resp.username  = rec.username;
        resp.fullName  = rec.fullName;
        resp.role      = rec.role;
        resp.createdAt = rec.createdAt;
        arr.push_back(nlohmann::json(resp));
    }

    nlohmann::json j;
    j["teachers"] = arr;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void TeacherController::handleGetTeacher(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // Protected but not admin-only — teachers can view their own profile
    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();
    auto result = m_teacherRepo.findById(id);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.teacher_not_found"));
        return;
    }

    const auto& rec = result.value();
    dto::TeacherResponse resp;
    resp.id        = rec.id;
    resp.username  = rec.username;
    resp.fullName  = rec.fullName;
    resp.role      = rec.role;
    resp.createdAt = rec.createdAt;

    const nlohmann::json j = resp;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void TeacherController::handleUpdateTeacher(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    const auto fullName = body.value("fullName", std::string{});
    const auto role     = body.value("role", std::string{});

    auto updateResult = m_teacherRepo.update(id, fullName, role);
    if (updateResult.is_err()) {
        sendError(res, 404, tr(lang, "error.teacher_not_found"));
        return;
    }

    auto fetched = m_teacherRepo.findById(id);
    if (fetched.is_err()) {
        sendError(res, 500, fetched.error().message);
        return;
    }

    const auto& rec = fetched.value();
    dto::TeacherResponse resp;
    resp.id        = rec.id;
    resp.username  = rec.username;
    resp.fullName  = rec.fullName;
    resp.role      = rec.role;
    resp.createdAt = rec.createdAt;

    const nlohmann::json j = resp;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

void TeacherController::handleDeleteTeacher(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();
    auto result = m_teacherRepo.remove(id);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.teacher_not_found"));
        return;
    }

    res.status = 204;
}

void TeacherController::handleAssignLocation(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string teacherId = req.matches[1].str();

    dto::AssignLocationRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::AssignLocationRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    // Verify teacher exists
    auto teacherResult = m_teacherRepo.findById(teacherId);
    if (teacherResult.is_err()) {
        sendError(res, 404, tr(lang, "error.teacher_not_found"));
        return;
    }

    // Verify location exists
    auto locationResult = m_locationRepo.findById(dto.locationId);
    if (locationResult.is_err()) {
        sendError(res, 404, tr(lang, "error.location_not_found"));
        return;
    }

    auto result = m_teacherLocationRepo.assign(teacherId, dto.locationId);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json j;
    j["teacherId"]   = teacherId;
    j["locationId"]  = dto.locationId;
    j["status"]      = "assigned";
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

void TeacherController::handleRevokeLocation(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireAdmin(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 2) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string teacherId  = req.matches[1].str();
    const std::string locationId = req.matches[2].str();

    auto result = m_teacherLocationRepo.revoke(teacherId, locationId);
    if (result.is_err()) {
        sendError(res, 404, tr(lang, "error.location_not_found"));
        return;
    }

    res.status = 204;
}

} // namespace hub32api::api::v1
