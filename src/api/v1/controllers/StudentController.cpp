#include "core/PrecompiledHeader.hpp"
#include "StudentController.hpp"
#include "../dto/StudentDto.hpp"
#include "db/StudentRepository.hpp"
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
 * @brief Extracts and validates a Bearer token, checks that the caller holds
 *        the "teacher" or "owner" role.
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

/**
 * @brief Maps a StudentRecord to the public StudentResponse DTO.
 */
hub32api::api::v1::dto::StudentResponse toDto(const hub32api::db::StudentRecord& rec)
{
    hub32api::api::v1::dto::StudentResponse resp;
    resp.id          = rec.id;
    resp.tenantId    = rec.tenantId;
    resp.classId     = rec.classId;
    resp.fullName    = rec.fullName;
    resp.username    = rec.username;
    resp.isActivated = rec.isActivated;
    resp.machineId   = rec.machineId;
    resp.createdAt   = rec.createdAt;
    return resp;
}

} // anonymous namespace

namespace hub32api::api::v1 {

StudentController::StudentController(db::StudentRepository& studentRepo,
                                     auth::JwtAuth& jwtAuth)
    : m_studentRepo(studentRepo)
    , m_jwtAuth(jwtAuth)
{}

// ─── POST /api/v1/students ────────────────────────────────────────────────────

void StudentController::handleCreate(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireTeacherOrOwner(req, res, m_jwtAuth, lang)) return;

    const std::string tenantId = getTenantId(req, m_jwtAuth);
    if (tenantId.empty()) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    dto::CreateStudentRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::CreateStudentRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.classId.empty()) {
        sendError(res, 400, "classId is required");
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

    // Check for existing student with same username in this tenant
    auto existing = m_studentRepo.findByUsername(tenantId, dto.username);
    if (existing.is_ok()) {
        sendError(res, 409, "Username already taken in this tenant");
        return;
    }

    auto result = m_studentRepo.create(tenantId, dto.classId, dto.fullName,
                                        dto.username, dto.password);
    if (result.is_err()) {
        const auto& err = result.error();
        if (err.code == hub32api::ErrorCode::Conflict) {
            sendError(res, 409, err.message);
        } else {
            sendError(res, 500, err.message);
        }
        return;
    }

    auto fetched = m_studentRepo.findById(result.value());
    if (fetched.is_err()) {
        sendError(res, 500, fetched.error().message);
        return;
    }

    const nlohmann::json j = toDto(fetched.value());
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

// ─── GET /api/v1/students?classId=xxx ────────────────────────────────────────

void StudentController::handleList(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireTeacherOrOwner(req, res, m_jwtAuth, lang)) return;

    const std::string classId = req.get_param_value("classId");
    if (classId.empty()) {
        sendError(res, 400, "classId query param required");
        return;
    }

    auto result = m_studentRepo.listByClass(classId);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& rec : result.value()) {
        arr.push_back(nlohmann::json(toDto(rec)));
    }

    nlohmann::json j;
    j["students"] = arr;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ─── GET /api/v1/students/:id ─────────────────────────────────────────────────

void StudentController::handleGet(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireTeacherOrOwner(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto result = m_studentRepo.findById(id);
    if (result.is_err()) {
        sendError(res, 404, "Student not found");
        return;
    }

    const nlohmann::json j = toDto(result.value());
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ─── PUT /api/v1/students/:id ────────────────────────────────────────────────

void StudentController::handleUpdate(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireTeacherOrOwner(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    dto::UpdateStudentRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::UpdateStudentRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    // Update fullName if provided
    if (!dto.fullName.empty()) {
        auto updateResult = m_studentRepo.update(id, dto.fullName);
        if (updateResult.is_err()) {
            const auto& err = updateResult.error();
            if (err.code == hub32api::ErrorCode::NotFound) {
                sendError(res, 404, "Student not found");
            } else {
                sendError(res, 500, err.message);
            }
            return;
        }
    }

    // Change password if provided
    if (!dto.password.empty()) {
        auto pwResult = m_studentRepo.changePassword(id, dto.password);
        if (pwResult.is_err()) {
            const auto& err = pwResult.error();
            if (err.code == hub32api::ErrorCode::NotFound) {
                sendError(res, 404, "Student not found");
            } else {
                sendError(res, 500, err.message);
            }
            return;
        }
    }

    // Return the updated record
    auto fetched = m_studentRepo.findById(id);
    if (fetched.is_err()) {
        sendError(res, 404, "Student not found");
        return;
    }

    const nlohmann::json j = toDto(fetched.value());
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ─── DELETE /api/v1/students/:id ─────────────────────────────────────────────

void StudentController::handleDelete(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireTeacherOrOwner(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto result = m_studentRepo.remove(id);
    if (result.is_err()) {
        sendError(res, 404, "Student not found");
        return;
    }

    res.status = 204;
}

// ─── POST /api/v1/students/:id/reset-machine ─────────────────────────────────

void StudentController::handleResetMachine(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (!requireTeacherOrOwner(req, res, m_jwtAuth, lang)) return;

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto result = m_studentRepo.resetMachine(id);
    if (result.is_err()) {
        sendError(res, 404, "Student not found");
        return;
    }

    nlohmann::json j;
    j["studentId"] = id;
    j["status"]    = "machine_reset";
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ─── POST /api/v1/students/activate ──────────────────────────────────────────

/**
 * @brief Student self-service endpoint: bind machine fingerprint.
 *
 * Auth: Bearer token with role="student".
 * Body: { "machineFingerprint": "<sha256hex>" }
 * The student's username is the JWT subject. We look up the student by
 * (tenantId, username) to resolve the UUID for the activation call.
 */
void StudentController::handleActivateMachine(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // Validate student Bearer token
    const std::string authHeader = req.get_header_value("Authorization");
    if (authHeader.size() <= hub32api::kBearerPrefixLen) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }
    const std::string rawToken = authHeader.substr(hub32api::kBearerPrefixLen);
    auto authResult = m_jwtAuth.authenticate(rawToken);
    if (authResult.is_err() || !authResult.value().token) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    const auto& ctx = authResult.value();
    if (ctx.token->role != "student") {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    const std::string tenantId = ctx.token->tenant_id;
    const std::string username = ctx.token->subject;

    if (tenantId.empty() || username.empty()) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    // Parse body
    dto::ActivateMachineRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::ActivateMachineRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.machineFingerprint.empty()) {
        sendError(res, 400, "machineFingerprint is required");
        return;
    }

    // Resolve student id from (tenantId, username)
    auto findResult = m_studentRepo.findByUsername(tenantId, username);
    if (findResult.is_err()) {
        sendError(res, 404, "Student not found");
        return;
    }

    const std::string studentId = findResult.value().id;

    auto activateResult = m_studentRepo.activate(studentId, dto.machineFingerprint);
    if (activateResult.is_err()) {
        const auto& err = activateResult.error();
        if (err.code == hub32api::ErrorCode::Conflict) {
            sendError(res, 409, "Machine already activated for this student");
        } else if (err.code == hub32api::ErrorCode::NotFound) {
            sendError(res, 404, "Student not found");
        } else {
            sendError(res, 500, err.message);
        }
        return;
    }

    nlohmann::json j;
    j["studentId"]          = studentId;
    j["machineFingerprint"] = dto.machineFingerprint;
    j["status"]             = "activated";
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

} // namespace hub32api::api::v1
