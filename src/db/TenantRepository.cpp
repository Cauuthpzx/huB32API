/**
 * @file TenantRepository.cpp
 * @brief CRUD and lifecycle operations for the tenants table using SQLite prepared statements.
 *
 * All SQL uses sqlite3_prepare_v2 / sqlite3_bind_* / sqlite3_step / sqlite3_finalize.
 * No string concatenation is used for SQL query construction.
 */

#include "../core/PrecompiledHeader.hpp"
#include "TenantRepository.hpp"
#include "DatabaseManager.hpp"

#include <sqlite3.h>
#include "core/internal/CryptoUtils.hpp"

namespace hub32api::db {

using hub32api::core::internal::CryptoUtils;

TenantRepository::TenantRepository(DatabaseManager& dbManager)
    : m_dbManager(dbManager)
    , m_db(dbManager.schoolDb())
{
}

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

/**
 * @brief Inserts a new tenant record with status='pending' and plan='trial'.
 *
 * @param name        Display name of the tenant organisation.
 * @param slug        Unique URL-safe identifier for the tenant.
 * @param ownerEmail  Email address of the owning user.
 * @return Result containing the new tenant UUID on success, or Conflict if the
 *         slug is already taken, or InternalError on DB failure.
 */
Result<std::string> TenantRepository::create(const std::string& name,
                                              const std::string& slug,
                                              const std::string& ownerEmail)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        spdlog::error("[TenantRepository] UUID generation failed: {}", uuidResult.error().message);
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "UUID generation failed"
        });
    }
    const std::string id = uuidResult.take();

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "INSERT INTO tenants(id, slug, name, owner_email, status, plan, created_at)"
        " VALUES(?, ?, ?, ?, 'pending', 'trial', ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TenantRepository] create prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(),         static_cast<int>(id.size()),         SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, slug.c_str(),        static_cast<int>(slug.size()),        SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, name.c_str(),        static_cast<int>(name.size()),        SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, ownerEmail.c_str(),  static_cast<int>(ownerEmail.size()),  SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, now);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        if (rc == SQLITE_CONSTRAINT) {
            const int extErr = sqlite3_extended_errcode(m_db);
            if (extErr == SQLITE_CONSTRAINT_UNIQUE) {
                const char* errmsg = sqlite3_errmsg(m_db);
                const std::string msg = errmsg ? errmsg : "";
                if (msg.find("owner_email") != std::string::npos) {
                    spdlog::warn("[TenantRepository] duplicate owner_email: {}", ownerEmail);
                    return Result<std::string>::fail(ApiError{
                        ErrorCode::Conflict,
                        "Email already registered: " + ownerEmail
                    });
                }
                spdlog::warn("[TenantRepository] duplicate slug: {}", slug);
                return Result<std::string>::fail(ApiError{
                    ErrorCode::Conflict,
                    "Slug already taken: " + slug
                });
            }
        }
        spdlog::error("[TenantRepository] create step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[TenantRepository] created tenant id={} slug={}", id, slug);
    return Result<std::string>::ok(std::move(id));
}

// ---------------------------------------------------------------------------
// activate
// ---------------------------------------------------------------------------

/**
 * @brief Sets a tenant's status to 'active' and records the activation timestamp.
 *
 * @param tenantId  UUID of the tenant to activate.
 * @return Result<void> on success, NotFound if the id does not exist,
 *         or InternalError on DB failure.
 */
Result<void> TenantRepository::activate(const std::string& tenantId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "UPDATE tenants SET status='active', activated_at=? WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TenantRepository] activate prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt, 2, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TenantRepository] activate step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Tenant not found: " + tenantId
        });
    }

    spdlog::debug("[TenantRepository] activated tenant id={}", tenantId);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// findById
// ---------------------------------------------------------------------------

/**
 * @brief Finds a tenant by its primary key.
 *
 * @param id  UUID of the tenant.
 * @return Result containing the TenantRecord, or NotFound if not present.
 */
Result<TenantRecord> TenantRepository::findById(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, slug, name, owner_email, status, plan, created_at, activated_at"
        " FROM tenants WHERE id=? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TenantRepository] findById prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<TenantRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        TenantRecord record;
        record.id          = col_text(0);
        record.slug        = col_text(1);
        record.name        = col_text(2);
        record.ownerEmail  = col_text(3);
        record.status      = col_text(4);
        record.plan        = col_text(5);
        record.createdAt   = sqlite3_column_int64(stmt, 6);
        record.activatedAt = sqlite3_column_int64(stmt, 7);  // 0 if NULL
        sqlite3_finalize(stmt);
        return Result<TenantRecord>::ok(std::move(record));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<TenantRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Tenant not found: " + id
        });
    }

    spdlog::error("[TenantRepository] findById step failed: {}", sqlite3_errmsg(m_db));
    return Result<TenantRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// findBySlug
// ---------------------------------------------------------------------------

/**
 * @brief Finds a tenant by its unique slug.
 *
 * @param slug  URL-safe identifier of the tenant.
 * @return Result containing the TenantRecord, or NotFound if not present.
 */
Result<TenantRecord> TenantRepository::findBySlug(const std::string& slug)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, slug, name, owner_email, status, plan, created_at, activated_at"
        " FROM tenants WHERE slug=? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TenantRepository] findBySlug prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<TenantRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, slug.c_str(), static_cast<int>(slug.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        TenantRecord record;
        record.id          = col_text(0);
        record.slug        = col_text(1);
        record.name        = col_text(2);
        record.ownerEmail  = col_text(3);
        record.status      = col_text(4);
        record.plan        = col_text(5);
        record.createdAt   = sqlite3_column_int64(stmt, 6);
        record.activatedAt = sqlite3_column_int64(stmt, 7);  // 0 if NULL
        sqlite3_finalize(stmt);
        return Result<TenantRecord>::ok(std::move(record));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<TenantRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Tenant not found by slug: " + slug
        });
    }

    spdlog::error("[TenantRepository] findBySlug step failed: {}", sqlite3_errmsg(m_db));
    return Result<TenantRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// suspend
// ---------------------------------------------------------------------------

/**
 * @brief Sets a tenant's status to 'suspended'.
 *
 * @param id  UUID of the tenant to suspend.
 * @return Result<void> on success, NotFound if the id does not exist.
 */
Result<void> TenantRepository::suspend(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE tenants SET status='suspended' WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TenantRepository] suspend prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TenantRepository] suspend step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Tenant not found: " + id
        });
    }

    spdlog::debug("[TenantRepository] suspended tenant id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// unsuspend
// ---------------------------------------------------------------------------

/**
 * @brief Restores a suspended tenant to 'active' status.
 *
 * @param id  UUID of the tenant to unsuspend.
 * @return Result<void> on success, NotFound if the id does not exist.
 */
Result<void> TenantRepository::unsuspend(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE tenants SET status='active' WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TenantRepository] unsuspend prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TenantRepository] unsuspend step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Tenant not found: " + id
        });
    }

    spdlog::debug("[TenantRepository] unsuspended tenant id={}", id);
    return Result<void>::ok();
}

} // namespace hub32api::db
