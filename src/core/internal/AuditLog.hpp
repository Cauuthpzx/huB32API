#pragma once

#include <memory>
#include <string>

namespace hub32api::core::internal {

/**
 * @brief A single audit log entry.
 */
struct AuditEntry
{
    std::string level;      ///< "info", "warn", "error"
    std::string category;   ///< "auth", "feature", "config", "system"
    std::string subject;    ///< Username or empty
    std::string action;     ///< Action performed
    std::string detail;     ///< Additional detail
    std::string ipAddress;  ///< Client IP or empty
    bool        success = true;
};

/**
 * @brief SQLite-backed audit log with background writer thread.
 *
 * Thread-safe: log() can be called from any thread. Writes are batched
 * in transactions on a dedicated writer thread for performance.
 */
class AuditLog
{
public:
    explicit AuditLog(const std::string& dbPath);
    ~AuditLog();

    /// @brief Enqueue a raw audit entry.
    void log(AuditEntry entry);

    /// @brief Log an authentication attempt.
    void logAuth(const std::string& subject, const std::string& ip,
                 bool success, const std::string& detail = {});

    /// @brief Log a logout event.
    void logLogout(const std::string& subject, const std::string& ip);

    /// @brief Log a feature operation (start/stop).
    void logFeatureOp(const std::string& subject, const std::string& computerId,
                      const std::string& featureId, const std::string& op);

    /// @brief Log an error event.
    void logError(const std::string& category, const std::string& detail,
                  const std::string& ip = {});

private:
    void writerLoop();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hub32api::core::internal
