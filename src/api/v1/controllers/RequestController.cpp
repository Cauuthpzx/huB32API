#include "core/PrecompiledHeader.hpp"
#include "RequestController.hpp"
#include "../dto/RequestDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "db/PendingRequestRepository.hpp"
#include "db/ClassRepository.hpp"
#include "db/StudentRepository.hpp"
#include "db/TeacherRepository.hpp"
#include "auth/JwtAuth.hpp"
#include "auth/UserRoleStore.hpp"
#include "core/internal/I18n.hpp"
#include "api/common/HttpErrorUtil.hpp"
#include "hub32api/core/Constants.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

using hub32api::api::common::sendError;
using hub32api::auth::UserRoleStore;

namespace {

std::string getLocale(const httplib::Request& req)
{
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

/**
 * @brief Extracts and validates the JWT, returning the AuthContext.
 *
 * Sends a 403 and returns nullopt if the token is absent or invalid.
 */
std::optional<hub32api::AuthContext>
requireAuth(const httplib::Request& req, httplib::Response& res,
            hub32api::auth::JwtAuth& jwtAuth, const std::string& lang)
{
    using hub32api::core::internal::tr;
    const std::string authHeader = req.get_header_value("Authorization");
    if (authHeader.size() <= hub32api::kBearerPrefixLen) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return std::nullopt;
    }
    const std::string token = authHeader.substr(hub32api::kBearerPrefixLen);
    auto result = jwtAuth.authenticate(token);
    if (result.is_err() || !result.value().token) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return std::nullopt;
    }
    return result.value();
}

hub32api::api::v1::dto::PendingRequestResponse toDto(
    const hub32api::db::PendingRequestRecord& rec)
{
    hub32api::api::v1::dto::PendingRequestResponse r;
    r.id         = rec.id;
    r.tenantId   = rec.tenantId;
    r.fromId     = rec.fromId;
    r.fromRole   = rec.fromRole;
    r.toId       = rec.toId;
    r.toRole     = rec.toRole;
    r.type       = rec.type;
    r.status     = rec.status;
    r.createdAt  = rec.createdAt;
    r.resolvedAt = rec.resolvedAt;
    return r;
}

} // anonymous namespace

namespace hub32api::api::v1 {

RequestController::RequestController(
    db::PendingRequestRepository& requestRepo,
    db::ClassRepository&          classRepo,
    db::StudentRepository&        studentRepo,
    db::TeacherRepository&        teacherRepo,
    auth::JwtAuth&                jwtAuth)
    : m_requestRepo(requestRepo)
    , m_classRepo(classRepo)
    , m_studentRepo(studentRepo)
    , m_teacherRepo(teacherRepo)
    , m_jwtAuth(jwtAuth)
{}

// ── POST /api/v1/requests/change-password ────────────────────────────────────

/**
 * @brief Student or teacher submits a password-change ticket.
 *
 * The plain-text password is hashed immediately; only the hash enters the DB.
 * Routing:
 *   - student  → to_id = class.teacher_id (or tenant_id if no teacher assigned)
 *   - teacher  → to_id = tenant_id (owner inbox)
 */
void RequestController::handleSubmitChangePassword(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    auto ctxOpt = requireAuth(req, res, m_jwtAuth, lang);
    if (!ctxOpt) return;
    const auto& ctx = *ctxOpt;

    const std::string role     = ctx.token->role;
    const std::string subject  = ctx.token->subject;
    const std::string tenantId = ctx.token->tenant_id;

    if (role != "student" && role != "teacher") {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    dto::SubmitChangePasswordRequest dto;
    try {
        const auto j = nlohmann::json::parse(req.body);
        dto = j.get<dto::SubmitChangePasswordRequest>();
    } catch (const std::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), ex.what());
        return;
    }

    if (dto.newPassword.empty()) {
        sendError(res, 400, "newPassword is required");
        return;
    }

    // Hash the plain-text password immediately — never store it.
    auto hashResult = UserRoleStore::hashPassword(dto.newPassword);
    if (hashResult.is_err()) {
        sendError(res, 500, "Password hashing failed");
        return;
    }
    const nlohmann::json payload = {{"password_hash", hashResult.value()}};

    std::string toId;
    std::string toRole;

    if (role == "student") {
        // Find the student's class, then the teacher assigned to it.
        auto studentResult = m_studentRepo.findById(subject);
        if (studentResult.is_err()) {
            sendError(res, 404, "Student not found");
            return;
        }
        const std::string classId = studentResult.value().classId;

        auto classResult = m_classRepo.findById(classId);
        if (classResult.is_err()) {
            sendError(res, 404, "Class not found");
            return;
        }
        const std::string teacherId = classResult.value().teacherId;

        if (!teacherId.empty()) {
            // Teacher is assigned — route to teacher.
            toId   = teacherId;
            toRole = "teacher";
        } else {
            // No teacher — route directly to owner (identified by tenant_id).
            toId   = tenantId;
            toRole = "owner";
        }
    } else {
        // Teacher submits — route to owner.
        toId   = tenantId;
        toRole = "owner";
    }

    auto result = m_requestRepo.submit(
        tenantId, subject, role, toId, toRole,
        "change_password", payload.dump());

    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json j;
    j["id"]     = result.value();
    j["status"] = "pending";
    res.status = 201;
    res.set_content(j.dump(), "application/json");
}

// ── GET /api/v1/requests/inbox ───────────────────────────────────────────────

/**
 * @brief Returns pending tickets addressed to the caller.
 *
 *   - teacher: tickets where to_id = subject (teacher UUID)
 *   - owner:   tickets where to_id = tenant_id
 */
void RequestController::handleListInbox(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    auto ctxOpt = requireAuth(req, res, m_jwtAuth, lang);
    if (!ctxOpt) return;
    const auto& ctx = *ctxOpt;

    const std::string role     = ctx.token->role;
    const std::string subject  = ctx.token->subject;
    const std::string tenantId = ctx.token->tenant_id;

    if (role != "teacher" && role != "owner") {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    // owner inbox uses tenant_id as the to_id; teacher uses their own UUID.
    const std::string inboxId = (role == "owner") ? tenantId : subject;

    auto result = m_requestRepo.listPendingForRecipient(inboxId);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& rec : result.value()) {
        arr.push_back(nlohmann::json(toDto(rec)));
    }

    nlohmann::json j;
    j["requests"] = arr;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ── GET /api/v1/requests/outbox ──────────────────────────────────────────────

/** @brief Returns all tickets submitted by the caller (any status). */
void RequestController::handleListOutbox(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    auto ctxOpt = requireAuth(req, res, m_jwtAuth, lang);
    if (!ctxOpt) return;
    const auto& ctx = *ctxOpt;

    const std::string subject = ctx.token->subject;

    auto result = m_requestRepo.listBySubmitter(subject);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& rec : result.value()) {
        arr.push_back(nlohmann::json(toDto(rec)));
    }

    nlohmann::json j;
    j["requests"] = arr;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

// ── POST /api/v1/requests/:id/accept ─────────────────────────────────────────

/**
 * @brief Accepts a ticket: applies the password_hash, then marks accepted.
 *
 * Authorization: only the intended recipient (to_id matches caller) may accept.
 */
void RequestController::handleAccept(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    auto ctxOpt = requireAuth(req, res, m_jwtAuth, lang);
    if (!ctxOpt) return;
    const auto& ctx = *ctxOpt;

    const std::string role     = ctx.token->role;
    const std::string subject  = ctx.token->subject;
    const std::string tenantId = ctx.token->tenant_id;

    if (role != "teacher" && role != "owner") {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto reqResult = m_requestRepo.findById(id);
    if (reqResult.is_err()) {
        sendError(res, 404, "Request not found");
        return;
    }
    const auto& pendingReq = reqResult.value();

    // Verify tenant isolation — caller must belong to the same tenant as the request.
    if (pendingReq.tenantId != tenantId) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    // Verify caller is the intended recipient.
    const std::string callerId = (role == "owner") ? tenantId : subject;
    if (pendingReq.toId != callerId) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }
    if (pendingReq.status != "pending") {
        sendError(res, 409, "Request is already resolved");
        return;
    }

    // Extract the password_hash from the payload.
    std::string passwordHash;
    try {
        const auto payload = nlohmann::json::parse(pendingReq.payload);
        passwordHash = payload.at("password_hash").get<std::string>();
    } catch (const std::exception& ex) {
        sendError(res, 500, "Malformed request payload");
        return;
    }

    if (passwordHash.rfind("$argon2id$", 0) != 0) {
        sendError(res, 500, "Malformed request payload: invalid password_hash format");
        return;
    }

    if (pendingReq.fromRole != "student" && pendingReq.fromRole != "teacher") {
        sendError(res, 500, "Unknown from_role in request");
        return;
    }

    // Atomically apply the hash and mark the request accepted in one transaction.
    auto acceptResult = m_requestRepo.acceptAndApplyPassword(
        id, pendingReq.fromRole, pendingReq.fromId, passwordHash);

    if (acceptResult.is_err()) {
        const auto& err = acceptResult.error();
        if (err.code == hub32api::ErrorCode::NotFound) {
            sendError(res, 404, err.message);
        } else if (err.code == hub32api::ErrorCode::InvalidRequest) {
            sendError(res, 400, err.message);
        } else {
            sendError(res, 500, err.message);
        }
        return;
    }

    res.status = 204;
}

// ── POST /api/v1/requests/:id/reject ─────────────────────────────────────────

/** @brief Rejects a pending ticket. No side effects beyond status update. */
void RequestController::handleReject(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    auto ctxOpt = requireAuth(req, res, m_jwtAuth, lang);
    if (!ctxOpt) return;
    const auto& ctx = *ctxOpt;

    const std::string role     = ctx.token->role;
    const std::string subject  = ctx.token->subject;
    const std::string tenantId = ctx.token->tenant_id;

    if (role != "teacher" && role != "owner") {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto reqResult = m_requestRepo.findById(id);
    if (reqResult.is_err()) {
        sendError(res, 404, "Request not found");
        return;
    }
    const auto& pendingReq = reqResult.value();

    // Verify tenant isolation — caller must belong to the same tenant as the request.
    if (pendingReq.tenantId != tenantId) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }

    // Verify caller is the intended recipient.
    const std::string callerId = (role == "owner") ? tenantId : subject;
    if (pendingReq.toId != callerId) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return;
    }
    if (pendingReq.status != "pending") {
        sendError(res, 409, "Request is already resolved");
        return;
    }

    auto result = m_requestRepo.reject(id);
    if (result.is_err()) {
        sendError(res, 500, result.error().message);
        return;
    }

    res.status = 204;
}

} // namespace hub32api::api::v1
