#pragma once
#include <string>
#include <memory>

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

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    void createSchema();
};

} // namespace hub32api::db
