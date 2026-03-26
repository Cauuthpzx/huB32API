/**
 * @file AuditLog.cpp
 * @brief SQLite-based audit log for recording security and operational events.
 *
 * Uses a dedicated writer thread with a lock-free queue to avoid blocking
 * HTTP request handlers. Events are batched and written in transactions
 * for performance.
 */

#include "PrecompiledHeader.hpp"
#include "internal/AuditLog.hpp"

#include <sqlite3.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace hub32api::core::internal {

namespace {

constexpr const char* k_createTableSql = R"(
    CREATE TABLE IF NOT EXISTS audit_log (
        id         INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp  TEXT    NOT NULL DEFAULT (datetime('now')),
        level      TEXT    NOT NULL,
        category   TEXT    NOT NULL,
        subject    TEXT,
        action     TEXT    NOT NULL,
        detail     TEXT,
        ip_address TEXT,
        success    INTEGER NOT NULL DEFAULT 1
    );
    CREATE INDEX IF NOT EXISTS idx_audit_timestamp ON audit_log(timestamp);
    CREATE INDEX IF NOT EXISTS idx_audit_category  ON audit_log(category);
)";

} // anonymous namespace

struct AuditLog::Impl
{
    sqlite3*                     db = nullptr;
    std::queue<AuditEntry>       queue;
    std::mutex                   mutex;
    std::condition_variable      cv;
    std::thread                  writer;
    std::atomic<bool>            stopping{false};
    std::string                  dbPath;
};

AuditLog::AuditLog(const std::string& dbPath)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->dbPath = dbPath;

    if (dbPath.empty()) {
        spdlog::info("[AuditLog] disabled (no database path configured)");
        return;
    }

    int rc = sqlite3_open(dbPath.c_str(), &m_impl->db);
    if (rc != SQLITE_OK) {
        spdlog::error("[AuditLog] failed to open database '{}': {}",
                      dbPath, sqlite3_errmsg(m_impl->db));
        sqlite3_close(m_impl->db);
        m_impl->db = nullptr;
        return;
    }

    // Enable WAL mode for concurrent reads
    sqlite3_exec(m_impl->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_impl->db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    // Create tables
    char* errMsg = nullptr;
    rc = sqlite3_exec(m_impl->db, k_createTableSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        spdlog::error("[AuditLog] failed to create tables: {}", errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
    }

    // Start background writer thread
    m_impl->writer = std::thread([this]{ writerLoop(); });

    spdlog::info("[AuditLog] initialized at '{}'", dbPath);
}

AuditLog::~AuditLog()
{
    m_impl->stopping = true;
    m_impl->cv.notify_all();
    if (m_impl->writer.joinable())
        m_impl->writer.join();
    if (m_impl->db)
        sqlite3_close(m_impl->db);
}

void AuditLog::log(AuditEntry entry)
{
    if (!m_impl->db) return;
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->queue.push(std::move(entry));
    }
    m_impl->cv.notify_one();
}

void AuditLog::logAuth(const std::string& subject, const std::string& ip,
                       bool success, const std::string& detail)
{
    log(AuditEntry{"info", "auth", subject, success ? "login" : "login_failed",
                   detail, ip, success});
}

void AuditLog::logLogout(const std::string& subject, const std::string& ip)
{
    log(AuditEntry{"info", "auth", subject, "logout", {}, ip, true});
}

void AuditLog::logFeatureOp(const std::string& subject, const std::string& computerId,
                            const std::string& featureId, const std::string& op)
{
    log(AuditEntry{"info", "feature", subject,
                   op + ":" + featureId, "computer=" + computerId, {}, true});
}

void AuditLog::logError(const std::string& category, const std::string& detail,
                        const std::string& ip)
{
    log(AuditEntry{"error", category, {}, "error", detail, ip, false});
}

void AuditLog::writerLoop()
{
    sqlite3_stmt* stmt = nullptr;
    const char* insertSql =
        "INSERT INTO audit_log (level, category, subject, action, detail, ip_address, success) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    int rc = sqlite3_prepare_v2(m_impl->db, insertSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("[AuditLog] failed to prepare statement: {}",
                      sqlite3_errmsg(m_impl->db));
        return;
    }

    while (true) {
        std::queue<AuditEntry> batch;
        {
            std::unique_lock lock(m_impl->mutex);
            m_impl->cv.wait(lock, [this] {
                return !m_impl->queue.empty() || m_impl->stopping;
            });
            std::swap(batch, m_impl->queue);
        }

        if (batch.empty() && m_impl->stopping) break;

        rc = sqlite3_exec(m_impl->db, "BEGIN", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            spdlog::error("[AuditLog] failed to begin transaction: {}",
                          sqlite3_errmsg(m_impl->db));
            continue;
        }

        while (!batch.empty()) {
            const auto& e = batch.front();
            sqlite3_bind_text(stmt, 1, e.level.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, e.category.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, e.subject.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, e.action.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5, e.detail.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, e.ipAddress.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt,  7, e.success ? 1 : 0);
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                spdlog::error("[AuditLog] failed to insert row: {}",
                              sqlite3_errmsg(m_impl->db));
            }
            sqlite3_reset(stmt);
            batch.pop();
        }

        rc = sqlite3_exec(m_impl->db, "COMMIT", nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            spdlog::error("[AuditLog] failed to commit transaction: {}",
                          sqlite3_errmsg(m_impl->db));
        }
    }

    if (stmt) sqlite3_finalize(stmt);
}

} // namespace hub32api::core::internal
