#pragma once
#include <string>
#include <mutex>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class DatabaseManager;

/**
 * @brief Represents a student account without the password hash.
 *
 * password_hash is intentionally excluded from this public struct.
 * All authentication is performed internally via findByUsernameWithHash().
 */
struct StudentRecord {
    std::string id;
    std::string tenantId;
    std::string classId;
    std::string fullName;
    std::string username;
    // NOTE: password_hash intentionally NOT included
    std::string machineId;      // empty string if not yet activated
    bool        isActivated = false;
    int64_t     activatedAt = 0; // unix epoch seconds; 0 if not activated
    int64_t     createdAt   = 0;
};

/**
 * @brief CRUD and authentication operations for the students table.
 *
 * Thread safety: all public methods acquire m_dbManager.dbMutex() at entry.
 * This class itself is NOT re-entrant — do not call public methods from
 * within other public methods without releasing the lock first.
 */
class HUB32API_EXPORT StudentRepository
{
public:
    explicit StudentRepository(DatabaseManager& dbManager);

    /**
     * @brief Creates a new student, hashing the password with argon2id via UserRoleStore.
     *
     * @return Result containing the new student UUID, or Conflict if username is already
     *         taken within the same tenant.
     */
    Result<std::string>  create(const std::string& tenantId, const std::string& classId,
                                 const std::string& fullName, const std::string& username,
                                 const std::string& password);

    /**
     * @brief Authenticates a student by username + password within a tenant.
     *
     * Returns AuthenticationFailed for BOTH "not found" and "wrong password" cases
     * to prevent username enumeration (anti-enumeration).
     */
    Result<StudentRecord> authenticate(const std::string& username, const std::string& password,
                                       const std::string& tenantId);

    /**
     * @brief Binds a student to a machine (one-time activation).
     *
     * Sets machine_id, is_activated=1, activated_at=now.
     * Returns Conflict if the student is already activated.
     */
    Result<void>         activate(const std::string& studentId, const std::string& machineId);

    /**
     * @brief Clears machine binding, allowing the student to activate on a new machine.
     *
     * Sets machine_id=NULL, is_activated=0, activated_at=NULL.
     */
    Result<void>         resetMachine(const std::string& studentId);

    /**
     * @brief Changes a student's password and resets machine binding.
     *
     * Forces re-activation on next login so the student must re-bind to a machine.
     */
    Result<void>         changePassword(const std::string& studentId, const std::string& newPassword);

    /**
     * @brief Applies a pre-computed argon2id hash directly to the password_hash column
     *        and resets machine binding (same side-effects as changePassword).
     *
     * Used by RequestController::handleAccept() to apply a password hash that was
     * pre-computed at ticket submission time and stored in pending_requests.payload.
     * The hash must already be in argon2id encoded format — it is NOT re-hashed.
     */
    Result<void>         applyPasswordHash(const std::string& studentId, const std::string& passwordHash);

    /** @brief Looks up a student by primary key. Does not include password_hash. */
    Result<StudentRecord> findById(const std::string& id);

    /** @brief Looks up a student by (tenantId, username). Does not include password_hash. */
    Result<StudentRecord> findByUsername(const std::string& tenantId, const std::string& username);

    /** @brief Returns all students in a class, ordered by full_name. */
    Result<std::vector<StudentRecord>> listByClass(const std::string& classId);

    /**
     * @brief Updates a student's display name.
     *
     * @param id       UUID of the student.
     * @param fullName New display name (must be non-empty; validation is the caller's responsibility).
     * @return Result<void> — fails with NotFound if no row was modified.
     */
    Result<void>         update(const std::string& id, const std::string& fullName);

    /** @brief Deletes a student by primary key. Returns NotFound if not present. */
    Result<void>         remove(const std::string& id);

private:
    DatabaseManager& m_dbManager;
    sqlite3*         m_db;

    // Internal record that also carries the password hash — used only for authenticate().
    struct StudentWithHash {
        StudentRecord record;
        std::string   passwordHash;
    };

    /**
     * @brief Internal query that fetches a student row including the password hash.
     *
     * NEVER expose StudentWithHash or its passwordHash field through the public API.
     */
    Result<StudentWithHash> findByUsernameWithHash(const std::string& tenantId,
                                                    const std::string& username);
};

} // namespace hub32api::db
