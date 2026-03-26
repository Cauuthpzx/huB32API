#pragma once
#include <mutex>
#include <string>
#include <vector>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::db {

class DatabaseManager;

struct ComputerRecord {
    std::string id;
    std::string locationId;    // empty string means unassigned (NULL in DB)
    std::string hostname;
    std::string macAddress;
    std::string ipLastSeen;
    std::string agentVersion;
    int64_t     lastHeartbeat = 0;
    std::string state;         // "offline", "online", "locked", "demo"
    int         positionX = 0;
    int         positionY = 0;
};

class HUB32API_EXPORT ComputerRepository
{
public:
    explicit ComputerRepository(DatabaseManager& dbManager);

    Result<std::string> create(const std::string& locationId, const std::string& hostname,
                                const std::string& macAddress);
    Result<std::string> createUnassigned(const std::string& hostname, const std::string& macAddress);
    Result<ComputerRecord> findById(const std::string& id);
    Result<ComputerRecord> findByHostname(const std::string& hostname);
    Result<ComputerRecord> findByMac(const std::string& mac);
    Result<std::vector<ComputerRecord>> listByLocation(const std::string& locationId);
    Result<std::vector<ComputerRecord>> listAll();
    Result<void> update(const std::string& id, const std::string& locationId,
                         const std::string& hostname, int posX, int posY);
    Result<void> updateState(const std::string& id, const std::string& state);
    Result<void> updateHeartbeat(const std::string& id, const std::string& ip,
                                  const std::string& agentVersion);
    Result<void> remove(const std::string& id);

private:
    DatabaseManager& m_dbManager;
    sqlite3* m_db;
};

} // namespace hub32api::db
