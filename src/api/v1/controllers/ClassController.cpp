#include "core/PrecompiledHeader.hpp"
#include "ClassController.hpp"
#include "../dto/ClassDto.hpp"
#include "db/ClassRepository.hpp"
#include "db/TeacherRepository.hpp"
#include "auth/JwtAuth.hpp"
#include "core/internal/I18n.hpp"
#include "api/common/HttpErrorUtil.hpp"
#include "hub32api/core/Constants.hpp"

#include <httplib.h>

using hub32api::api::common::sendError;

namespace {

std::string getLocale(const httplib::Request& req)
{
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

/**
 * @brief Authenticates the request and checks the caller holds "teacher" or "owner" role.
 *
 * @return true if authorized; false if a 403 response has already been sent.
 */
bool requireTeacherOrOwner(const httplib::Request& req, httplib::Response& res,
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
    if (role != "teacher" && role != "owner") {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return false;
    }
    return true;
}

/**
 * @brief Authenticates the request and checks the caller holds the "owner" role.
 *
 * @return true if authorized; false if a 403 response has already been sent.
 */
bool requireOwner(const httplib::Request& req, httplib::Response& res,
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
    if (result.value().token->role != "owner") {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return false;
    }
    return true;
}

/**
 * @brief Extracts the tenant_id claim from the Bearer token.
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

/**
 * @brief Maps a ClassRecord to the public ClassResponse DTO.
 */
hub32api::api::v1::dto::ClassResponse toDto(const hub32api::db::ClassRecord& rec)
{
    hub32api::api::v1::dto::ClassResponse resp;
    resp.id        = rec.id;
    resp.tenantId  = rec.tenantId;
    resp.schoolId  = rec.schoolId;
    resp.name      = rec.name;
    resp.teacherId = rec.teacherId;
    resp.createdAt = rec.createdAt;
    return resp;
}

} // anonymous namespace

namespace hub32api::api::v1 {

ClassController::ClassController(db::ClassRepository&   classRepo,
                                 db::TeacherRepository& teacherRepo,
                                 auth::JwtAuth&         jwtAuth)
    : m_classRepo(classRepo)
    , m_teacherRepo(teacherRepo)
    , m_jwtAuth(jwtAuth)
{}

// ─── POST /api/v1/classes ─────────────────────────────────────────────────────

void ClassController::handleCreate(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireOwner(req, res, m_jwtAuth, lang)) return;

    const std::string tenantId = getTenantId(req, m_jwtAuth);
    if (tenantId.empty()) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    dto::CreateClassRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::CreateClassRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.schoolId.empty()) {
        sendError(res, 400, "schoolId is required");
        return;
    }
    if (dto.name.empty()) {
        sendError(res, 400, "name is required");
        return;
    }

    auto result = m_classRepo.create(tenantId, dto.schoolId, dto.name, dto.teacherId);
    if (result.is_err()) {
        const auto& err = result.error();
        if (err.code == hub32api::ErrorCode::Conflict) {
            sendError(res, 409, err.message);
        } else {
            sendError(res, 500, err.message);
        }
        return;
    }

    auto fetched = m_classRepo.findById(result.value());
    if (fetched.is_err()) {
        sendError(res, 500, fetched.error().message);
        return;
    }

    const nlohmann::json j = toDto(fetched.value());
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

// ─── GET /api/v1/classes ──────────────────────────────────────────────────────
//
// Owner: returns all classes in the tenant (listByTenant).
// Teacher: returns only classes assigned to that teacher (listByTeacher).
//          The JWT subject for a teacher is the teacher's UUID (set by AuthController
//          using rec.id from TeacherRepository::authenticate).

void ClassController::handleList(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireTeacherOrOwner(req, res, m_jwtAuth, lang)) return;

    // Re-authenticate to extract role and subject — token is already validated above.
    const std::string authHeader = req.get_header_value("Authorization");
    const std::string rawToken   = authHeader.substr(hub32api::kBearerPrefixLen);
    auto authCtx = m_jwtAuth.authenticate(rawToken);
    // authCtx is valid: requireTeacherOrOwner already confirmed it
    const auto& tokenObj  = authCtx.value().token;
    const std::string role     = tokenObj->role;
    const std::string subject  = tokenObj->subject;
    const std::string tenantId = tokenObj->tenant_id;

    Result<std::vector<db::ClassRecord>> result =
        (role == "owner")
            ? m_classRepo.listByTenant(tenantId)
            : m_classRepo.listByTeacher(subject);   // subject = teacher UUID

    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& rec : result.value()) {
        arr.push_back(nlohmann::json(toDto(rec)));
    }

    nlohmann::json j;
    j["classes"] = arr;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ─── GET /api/v1/classes/:id ──────────────────────────────────────────────────

void ClassController::handleGet(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireTeacherOrOwner(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto result = m_classRepo.findById(id);
    if (result.is_err()) {
        sendError(res, 404, "Class not found");
        return;
    }

    const nlohmann::json j = toDto(result.value());
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ─── PUT /api/v1/classes/:id ──────────────────────────────────────────────────

void ClassController::handleUpdate(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireOwner(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    dto::UpdateClassRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::UpdateClassRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.name.empty()) {
        sendError(res, 400, "name is required");
        return;
    }

    auto result = m_classRepo.update(id, dto.name, dto.teacherId);
    if (result.is_err()) {
        const auto& err = result.error();
        if (err.code == hub32api::ErrorCode::NotFound) {
            sendError(res, 404, "Class not found");
        } else {
            sendError(res, 500, err.message);
        }
        return;
    }

    auto fetched = m_classRepo.findById(id);
    if (fetched.is_err()) {
        sendError(res, 404, "Class not found");
        return;
    }

    const nlohmann::json j = toDto(fetched.value());
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ─── DELETE /api/v1/classes/:id ───────────────────────────────────────────────

void ClassController::handleDelete(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireOwner(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto result = m_classRepo.remove(id);
    if (result.is_err()) {
        sendError(res, 404, "Class not found");
        return;
    }

    res.status = 204;
}

} // namespace hub32api::api::v1
