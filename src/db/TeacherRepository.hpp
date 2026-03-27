#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class DatabaseManager;

struct TeacherRecord {
    std::string id;
    std::string username;
    std::string fullName;
    std::string role;          // "owner", "admin" or "teacher"
    std::string tenantId;      // tenant this teacher belongs to
    int64_t     createdAt = 0;
    // NOTE: password_hash intentionally NOT included — never expose to API
};

class HUB32API_EXPORT TeacherRepository
{
public:
    explicit TeacherRepository(DatabaseManager& dbManager);

    Result<std::string> create(const std::string& username, const std::string& password,
                                const std::string& fullName, const std::string& role = "teacher");
    Result<TeacherRecord> findById(const std::string& id);
    Result<TeacherRecord> findByUsername(const std::string& username);
    /**
     * @brief Authenticates a teacher by username/password, optionally scoped to a tenant.
     *
     * When tenantId is non-empty, the query adds AND tenant_id=? so only teachers
     * belonging to that tenant are matched (multi-tenant login).
     * When tenantId is empty, any teacher matching the username is checked
     * (used as a fallback path for non-tenant roles, kept for backwards compat).
     *
     * Returns the full TeacherRecord (id, username, role, etc.) on success so callers
     * can embed the correct role claim in the JWT without a second query.
     *
     * SECURITY: Returns AuthenticationFailed for BOTH "not found" and "wrong password"
     * to prevent username enumeration attacks.
     */
    Result<TeacherRecord> authenticate(const std::string& username, const std::string& password,
                                       const std::string& tenantId = "");
    /// When tenantId is non-empty, filters to teachers belonging to that tenant.
    /// When tenantId is empty (superadmin), returns all teachers.
    Result<std::vector<TeacherRecord>> listAll(const std::string& tenantId = "");
    Result<void> update(const std::string& id, const std::string& fullName, const std::string& role);
    Result<void> changePassword(const std::string& id, const std::string& newPassword);

    /**
     * @brief Applies a pre-computed argon2id hash directly to the password_hash column.
     *
     * Used by RequestController::handleAccept() to apply a password hash that was
     * pre-computed at ticket submission time and stored in pending_requests.payload.
     * The hash must already be in argon2id encoded format — it is NOT re-hashed.
     */
    Result<void> applyPasswordHash(const std::string& id, const std::string& passwordHash);

    Result<void> remove(const std::string& id);

private:
    DatabaseManager& m_dbManager;
    sqlite3* m_db;
};

} // namespace hub32api::db
