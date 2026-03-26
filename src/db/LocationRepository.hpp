#pragma once
#include <string>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

struct LocationRecord {
    std::string id;
    std::string schoolId;
    std::string name;
    std::string building;
    int         floor = 0;
    int         capacity = 0;
    std::string type; // "classroom", "lab", "office"
};

class HUB32API_EXPORT LocationRepository
{
public:
    explicit LocationRepository(sqlite3* db);

    Result<std::string> create(const std::string& schoolId, const std::string& name,
                                const std::string& building, int floor, int capacity,
                                const std::string& type = "classroom");
    Result<LocationRecord> findById(const std::string& id);
    Result<std::vector<LocationRecord>> listBySchool(const std::string& schoolId);
    Result<std::vector<LocationRecord>> listAll();
    Result<void> update(const std::string& id, const std::string& name,
                         const std::string& building, int floor, int capacity,
                         const std::string& type);
    Result<void> remove(const std::string& id);

private:
    sqlite3* m_db;
};

} // namespace hub32api::db
