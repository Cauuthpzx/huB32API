#pragma once
#include <string>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class DatabaseManager;

/**
 * @brief A single pending request from a subordinate to their superior.
 *
 * Routing rules (enforced by business logic, not DB):
 *   - student  → teacher of their class  (or owner if class has no teacher)
 *   - teacher  → owner of their tenant
 *
 * Currently the only supported type is "change_password"; the payload JSON
 * contains the pre-hashed password: {"password_hash": "argon2id:..."}.
 */
struct PendingRequestRecord {
    std::string id;
    std::string tenantId;
    std::string fromId;     ///< UUID of originator (student_id or teacher_id)
    std::string fromRole;   ///< "student" | "teacher"
    std::string toId;       ///< UUID of recipient (teacher_id or tenant_id for owner)
    std::string toRole;     ///< "teacher" | "owner"
    std::string type;       ///< "change_password"
    std::string payload;    ///< JSON blob, e.g. {"password_hash":"argon2id:..."}
    std::string status;     ///< "pending" | "accepted" | "rejected"
    int64_t     createdAt  = 0;
    int64_t     resolvedAt = 0; ///< 0 if not yet resolved
};

/**
 * @brief Repository for the pending_requests table.
 *
 * Thread safety: all public methods acquire DatabaseManager::dbMutex() at entry.
 */
class HUB32API_EXPORT PendingRequestRepository
{
public:
    explicit PendingRequestRepository(DatabaseManager& dbManager);

    /**
     * @brief Submits a new pending request.
     *
     * Automatically cancels (sets status='rejected') any existing pending request
     * of the same type from the same sender, so there is at most one active request
     * per (from_id, type) pair at any time.
     *
     * @return Result containing the new UUID, or error on DB failure.
     */
    Result<std::string> submit(const std::string& tenantId,
                                const std::string& fromId,
                                const std::string& fromRole,
                                const std::string& toId,
                                const std::string& toRole,
                                const std::string& type,
                                const std::string& payload);

    /**
     * @brief Returns all pending requests (status='pending') addressed to @p toId,
     *        ordered by created_at descending (newest first).
     */
    Result<std::vector<PendingRequestRecord>> listPendingForRecipient(const std::string& toId);

    /**
     * @brief Returns all requests (any status) submitted by @p fromId,
     *        ordered by created_at descending.
     */
    Result<std::vector<PendingRequestRecord>> listBySubmitter(const std::string& fromId);

    /** @brief Looks up a single request by primary key. */
    Result<PendingRequestRecord> findById(const std::string& id);

    /**
     * @brief Atomically applies a password hash and marks the request accepted.
     *
     * Runs BEGIN / UPDATE password_hash / UPDATE status=accepted / COMMIT in a
     * single SQLite transaction so that the password change and status update are
     * never observed in an inconsistent state.
     *
     * @param id            UUID of the pending_request row (must be 'pending').
     * @param fromRole      "student" | "teacher" — determines which table to update.
     * @param fromId        UUID of the originator — used as the WHERE key.
     * @param passwordHash  Pre-computed argon2id hash to write into password_hash.
     *                      Must begin with "$argon2id$"; validated before writing.
     * @return NotFound if the request does not exist or is not pending.
     * @return InvalidInput if passwordHash does not start with "$argon2id$".
     */
    Result<void> acceptAndApplyPassword(const std::string& id,
                                         const std::string& fromRole,
                                         const std::string& fromId,
                                         const std::string& passwordHash);

    /**
     * @brief Accepts a pending request: marks it accepted, sets resolved_at=now.
     *
     * Use acceptAndApplyPassword() for change_password requests.
     * This overload is for request types that have no DB side-effects.
     *
     * @return NotFound if the request does not exist or is not in 'pending' status.
     */
    Result<void> accept(const std::string& id);

    /**
     * @brief Rejects a pending request: marks it rejected, sets resolved_at=now.
     *
     * @return NotFound if the request does not exist or is not in 'pending' status.
     */
    Result<void> reject(const std::string& id);

private:
    DatabaseManager& m_dbManager;
    sqlite3*         m_db;
};

} // namespace hub32api::db
