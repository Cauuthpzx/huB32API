/**
 * @file PendingRequestRepository.cpp
 * @brief CRUD operations for the pending_requests table.
 *
 * Supports the ticket/inbox flow where subordinates (students, teachers) submit
 * requests (currently only "change_password") that must be approved by their
 * direct superior before taking effect.
 *
 * Routing:
 *   student  → teacher of their class  (or owner if class has no teacher)
 *   teacher  → owner  (to_id = tenant_id)
 *
 * All SQL uses sqlite3_prepare_v2 / sqlite3_bind_* — no string concatenation
 * is used for query construction.
 */

#include "../core/PrecompiledHeader.hpp"
#include "PendingRequestRepository.hpp"
#include "DatabaseManager.hpp"

#include <sqlite3.h>
#include "core/internal/CryptoUtils.hpp"

namespace hub32api::db {

using hub32api::core::internal::CryptoUtils;

PendingRequestRepository::PendingRequestRepository(DatabaseManager& dbManager)
    : m_dbManager(dbManager)
    , m_db(dbManager.schoolDb())
{
}

// ---------------------------------------------------------------------------
// Helper: map a sqlite3_stmt row → PendingRequestRecord
// ---------------------------------------------------------------------------

namespace {

PendingRequestRecord rowToRecord(sqlite3_stmt* stmt)
{
    auto col_text = [&](int col) -> std::string {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return txt ? std::string(txt) : std::string{};
    };

    PendingRequestRecord rec;
    rec.id         = col_text(0);
    rec.tenantId   = col_text(1);
    rec.fromId     = col_text(2);
    rec.fromRole   = col_text(3);
    rec.toId       = col_text(4);
    rec.toRole     = col_text(5);
    rec.type       = col_text(6);
    rec.payload    = col_text(7);
    rec.status     = col_text(8);
    rec.createdAt  = sqlite3_column_int64(stmt, 9);
    rec.resolvedAt = sqlite3_column_type(stmt, 10) != SQLITE_NULL
                     ? sqlite3_column_int64(stmt, 10)
                     : 0;
    return rec;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// submit
// ---------------------------------------------------------------------------

/**
 * @brief Submits a new pending request, auto-cancelling any existing pending
 *        request of the same (from_id, type) pair.
 *
 * The cancellation sets the old row's status to 'rejected' so it appears in
 * history but is no longer actionable.
 */
Result<std::string> PendingRequestRepository::submit(
    const std::string& tenantId,
    const std::string& fromId,
    const std::string& fromRole,
    const std::string& toId,
    const std::string& toRole,
    const std::string& type,
    const std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "UUID generation failed"
        });
    }
    const std::string id = uuidResult.take();

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Cancel any existing pending request of the same type from the same sender.
    constexpr const char* k_cancel_sql =
        "UPDATE pending_requests SET status='rejected', resolved_at=?"
        " WHERE from_id=? AND type=? AND status='pending';";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_cancel_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] submit cancel prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, fromId.c_str(), static_cast<int>(fromId.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, type.c_str(),   static_cast<int>(type.size()),   SQLITE_STATIC);
    sqlite3_step(stmt);  // non-fatal if 0 rows updated
    sqlite3_finalize(stmt);

    // Insert the new request.
    constexpr const char* k_insert_sql =
        "INSERT INTO pending_requests"
        "(id, tenant_id, from_id, from_role, to_id, to_role, type, payload, status, created_at)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?, 'pending', ?);";

    if (sqlite3_prepare_v2(m_db, k_insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] submit insert prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(),       static_cast<int>(id.size()),       SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, fromId.c_str(),   static_cast<int>(fromId.size()),   SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, fromRole.c_str(), static_cast<int>(fromRole.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, toId.c_str(),     static_cast<int>(toId.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, toRole.c_str(),   static_cast<int>(toRole.size()),   SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, type.c_str(),     static_cast<int>(type.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, payload.c_str(),  static_cast<int>(payload.size()),  SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 9, now);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[PendingRequestRepository] submit insert step failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[PendingRequestRepository] submitted request id={} type={} from={}",
                  id, type, fromId);
    return Result<std::string>::ok(std::move(id));
}

// ---------------------------------------------------------------------------
// listPendingForRecipient
// ---------------------------------------------------------------------------

Result<std::vector<PendingRequestRecord>>
PendingRequestRepository::listPendingForRecipient(const std::string& toId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, tenant_id, from_id, from_role, to_id, to_role, type, payload,"
        "       status, created_at, resolved_at"
        " FROM pending_requests WHERE to_id=? AND status='pending'"
        " ORDER BY created_at DESC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] listPendingForRecipient prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<std::vector<PendingRequestRecord>>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, toId.c_str(), static_cast<int>(toId.size()), SQLITE_STATIC);

    std::vector<PendingRequestRecord> result;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        result.push_back(rowToRecord(stmt));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[PendingRequestRepository] listPendingForRecipient step failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<std::vector<PendingRequestRecord>>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<PendingRequestRecord>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// listBySubmitter
// ---------------------------------------------------------------------------

Result<std::vector<PendingRequestRecord>>
PendingRequestRepository::listBySubmitter(const std::string& fromId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, tenant_id, from_id, from_role, to_id, to_role, type, payload,"
        "       status, created_at, resolved_at"
        " FROM pending_requests WHERE from_id=?"
        " ORDER BY created_at DESC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] listBySubmitter prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<std::vector<PendingRequestRecord>>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, fromId.c_str(), static_cast<int>(fromId.size()), SQLITE_STATIC);

    std::vector<PendingRequestRecord> result;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        result.push_back(rowToRecord(stmt));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[PendingRequestRepository] listBySubmitter step failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<std::vector<PendingRequestRecord>>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<PendingRequestRecord>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// findById
// ---------------------------------------------------------------------------

Result<PendingRequestRecord> PendingRequestRepository::findById(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, tenant_id, from_id, from_role, to_id, to_role, type, payload,"
        "       status, created_at, resolved_at"
        " FROM pending_requests WHERE id=? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] findById prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<PendingRequestRecord>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto rec = rowToRecord(stmt);
        sqlite3_finalize(stmt);
        return Result<PendingRequestRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<PendingRequestRecord>::fail(ApiError{
            ErrorCode::NotFound, "Request not found: " + id
        });
    }

    spdlog::error("[PendingRequestRepository] findById step failed: {}", sqlite3_errmsg(m_db));
    return Result<PendingRequestRecord>::fail(ApiError{
        ErrorCode::InternalError, sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// acceptAndApplyPassword
// ---------------------------------------------------------------------------

/**
 * @brief Atomically applies a password hash and marks the request accepted.
 *
 * Uses a SQLite BEGIN / UPDATE / UPDATE / COMMIT transaction so the two
 * mutations are never observed in an inconsistent state.  The lock is held
 * for the duration of the transaction.
 *
 * Validates that @p passwordHash begins with "$argon2id$" before writing.
 */
Result<void> PendingRequestRepository::acceptAndApplyPassword(
    const std::string& id,
    const std::string& fromRole,
    const std::string& fromId,
    const std::string& passwordHash)
{
    // Validate hash format before touching the DB.
    if (passwordHash.rfind("$argon2id$", 0) != 0) {
        spdlog::error("[PendingRequestRepository] acceptAndApplyPassword: invalid hash format");
        return Result<void>::fail(ApiError{
            ErrorCode::InvalidRequest, "password_hash is not a valid argon2id encoded string"
        });
    }

    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Begin transaction.
    if (sqlite3_exec(m_db, "BEGIN;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] acceptAndApplyPassword BEGIN failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    auto rollback = [&]() {
        sqlite3_exec(m_db, "ROLLBACK;", nullptr, nullptr, nullptr);
    };

    // Step 1: update the password_hash in the appropriate table.
    const char* k_pw_sql = (fromRole == "student")
        ? "UPDATE students SET password_hash=?, machine_id=NULL, is_activated=0, activated_at=NULL WHERE id=?;"
        : "UPDATE teachers SET password_hash=? WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_pw_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] acceptAndApplyPassword pw prepare failed: {}",
                      sqlite3_errmsg(m_db));
        rollback();
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, passwordHash.c_str(),
                      static_cast<int>(passwordHash.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fromId.c_str(),
                      static_cast<int>(fromId.size()), SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[PendingRequestRepository] acceptAndApplyPassword pw step failed: {}",
                      sqlite3_errmsg(m_db));
        rollback();
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }
    if (sqlite3_changes(m_db) == 0) {
        rollback();
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound, "Subject not found: " + fromId
        });
    }

    // Step 2: mark request as accepted.
    constexpr const char* k_accept_sql =
        "UPDATE pending_requests SET status='accepted', resolved_at=?"
        " WHERE id=? AND status='pending';";

    if (sqlite3_prepare_v2(m_db, k_accept_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] acceptAndApplyPassword accept prepare failed: {}",
                      sqlite3_errmsg(m_db));
        rollback();
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[PendingRequestRepository] acceptAndApplyPassword accept step failed: {}",
                      sqlite3_errmsg(m_db));
        rollback();
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }
    if (sqlite3_changes(m_db) == 0) {
        rollback();
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound, "Request not found or already resolved: " + id
        });
    }

    if (sqlite3_exec(m_db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] acceptAndApplyPassword COMMIT failed: {}",
                      sqlite3_errmsg(m_db));
        rollback();
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[PendingRequestRepository] acceptAndApplyPassword: request id={} accepted, "
                  "password applied to {} id={}", id, fromRole, fromId);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// accept
// ---------------------------------------------------------------------------

/**
 * @brief Marks the request as accepted.
 *
 * NOTE: The controller must apply the payload (e.g. update password_hash in
 *       students/teachers table) BEFORE calling accept(), so that a DB failure
 *       on the status update doesn't leave the password already changed.
 */
Result<void> PendingRequestRepository::accept(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "UPDATE pending_requests SET status='accepted', resolved_at=?"
        " WHERE id=? AND status='pending';";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] accept prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[PendingRequestRepository] accept step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound, "Request not found or not pending: " + id
        });
    }

    spdlog::debug("[PendingRequestRepository] accepted request id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// reject
// ---------------------------------------------------------------------------

Result<void> PendingRequestRepository::reject(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "UPDATE pending_requests SET status='rejected', resolved_at=?"
        " WHERE id=? AND status='pending';";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[PendingRequestRepository] reject prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[PendingRequestRepository] reject step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound, "Request not found or not pending: " + id
        });
    }

    spdlog::debug("[PendingRequestRepository] rejected request id={}", id);
    return Result<void>::ok();
}

} // namespace hub32api::db
