#pragma once
#include <string>
#include <memory>
#include <mutex>

#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class HUB32API_EXPORT DatabaseManager
{
public:
    explicit DatabaseManager(const std::string& dataDir);
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool isOpen() const noexcept;
    sqlite3* schoolDb() noexcept;

    /// @brief Returns a mutex that MUST be held during any sqlite3 operation.
    /// All repositories must lock this before calling sqlite3_prepare/step/etc.
    std::mutex& dbMutex() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    void createSchema();
    void runMigrations();
};

} // namespace hub32api::db
