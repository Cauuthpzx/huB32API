#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace hub32api::api::v1::dto {

// ── Submitted by student or teacher ─────────────────────────────────────────

/**
 * @brief Body sent when submitting a change_password ticket.
 *
 * The server hashes the plain-text password immediately on receipt;
 * the hash is stored in pending_requests.payload — the plain-text
 * is never persisted.
 */
struct SubmitChangePasswordRequest {
    std::string newPassword;  ///< plain-text; hashed server-side before storage
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SubmitChangePasswordRequest, newPassword)

// ── Response for a single pending request ───────────────────────────────────

struct PendingRequestResponse {
    std::string id;
    std::string tenantId;
    std::string fromId;
    std::string fromRole;
    std::string toId;
    std::string toRole;
    std::string type;
    std::string status;
    int64_t     createdAt  = 0;
    int64_t     resolvedAt = 0;
    // NOTE: payload (password_hash) is intentionally NOT exposed in responses
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PendingRequestResponse,
    id, tenantId, fromId, fromRole, toId, toRole, type, status, createdAt, resolvedAt)

} // namespace hub32api::api::v1::dto
