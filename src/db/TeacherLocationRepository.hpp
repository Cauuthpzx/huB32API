#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class DatabaseManager;

struct LocationRecord; // forward-declare from LocationRepository.hpp
struct TeacherRecord;  // forward-declare from TeacherRepository.hpp

class HUB32API_EXPORT TeacherLocationRepository
{
public:
    explicit TeacherLocationRepository(DatabaseManager& dbManager);

    Result<void> assign(const std::string& teacherId, const std::string& locationId);
    Result<void> revoke(const std::string& teacherId, const std::string& locationId);
    bool hasAccess(const std::string& teacherId, const std::string& locationId);
    std::vector<std::string> getLocationIdsForTeacher(const std::string& teacherId);
    std::vector<std::string> getTeacherIdsForLocation(const std::string& locationId);

private:
    DatabaseManager& m_dbManager;
    sqlite3* m_db;
};

} // namespace hub32api::db
