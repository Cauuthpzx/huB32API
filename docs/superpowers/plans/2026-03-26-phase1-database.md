# Phase 1: Database + School Model — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace all mock/stub data with a real SQLite-backed school model. Create repositories for schools, locations, computers, teachers, and teacher-location assignments. Add CRUD REST APIs. Implement role-based access control (admin=all rooms, teacher=assigned rooms only).

**Architecture:** Repository pattern over SQLite3. Each repository owns its prepared statements. A `DatabaseManager` class manages SQLite connections and schema migrations. Controllers delegate to repositories. The existing `Hub32CoreWrapper` stub and `NetworkDirectoryBridge` mock are replaced by `ComputerRepository`.

**Tech Stack:** C++17, SQLite3 (WAL mode), prepared statements, GoogleTest

**Depends on:** Phase 0 (security fixes applied)

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Create | `src/db/DatabaseManager.hpp` | Open/close SQLite, run migrations, provide db handle |
| Create | `src/db/DatabaseManager.cpp` | Implementation |
| Create | `src/db/SchoolRepository.hpp` | CRUD schools table |
| Create | `src/db/SchoolRepository.cpp` | Implementation |
| Create | `src/db/LocationRepository.hpp` | CRUD locations table |
| Create | `src/db/LocationRepository.cpp` | Implementation |
| Create | `src/db/ComputerRepository.hpp` | CRUD computers, state tracking from heartbeats |
| Create | `src/db/ComputerRepository.cpp` | Implementation |
| Create | `src/db/TeacherRepository.hpp` | CRUD teachers, password hashing delegation |
| Create | `src/db/TeacherRepository.cpp` | Implementation |
| Create | `src/db/TeacherLocationRepository.hpp` | Assign/revoke teacher↔location |
| Create | `src/db/TeacherLocationRepository.cpp` | Implementation |
| Create | `src/db/CMakeLists.txt` | Build db library |
| Create | `src/api/v1/controllers/SchoolController.hpp` | CRUD /api/v1/schools |
| Create | `src/api/v1/controllers/SchoolController.cpp` | Implementation |
| Create | `src/api/v1/controllers/TeacherController.hpp` | CRUD /api/v1/teachers |
| Create | `src/api/v1/controllers/TeacherController.cpp` | Implementation |
| Create | `src/api/v1/dto/SchoolDto.hpp` | School/Location DTOs |
| Create | `src/api/v1/dto/TeacherDto.hpp` | Teacher DTOs |
| Modify | `src/api/v1/controllers/ComputerController.cpp` | Use ComputerRepository instead of mock |
| Modify | `src/api/v2/controllers/LocationController.cpp` | Use LocationRepository |
| Modify | `src/plugins/computer/ComputerPlugin.cpp` | Use ComputerRepository |
| Modify | `src/server/HttpServer.cpp` | Create DatabaseManager, inject repos |
| Modify | `src/server/Router.cpp` | Register new routes |
| Modify | `src/server/internal/Router.hpp` | Add repo refs to Services struct |
| Modify | `include/hub32api/config/ServerConfig.hpp` | Add databaseDir field |
| Modify | `src/config/ServerConfig.cpp` | Parse databaseDir |
| Create | `tests/unit/db/test_school_repository.cpp` | Test SchoolRepository |
| Create | `tests/unit/db/test_location_repository.cpp` | Test LocationRepository |
| Create | `tests/unit/db/test_computer_repository.cpp` | Test ComputerRepository |
| Create | `tests/unit/db/test_teacher_repository.cpp` | Test TeacherRepository |
| Modify | `locales/en.json`, `vi.json`, `zh_CN.json` | New error/success keys |

---

### Task 1: DatabaseManager

**Files:**
- Create: `src/db/DatabaseManager.hpp`
- Create: `src/db/DatabaseManager.cpp`
- Create: `src/db/CMakeLists.txt`
- Modify: `src/CMakeLists.txt` — add `add_subdirectory(db)`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/unit/db/test_database_manager.cpp
#include <gtest/gtest.h>
#include "db/DatabaseManager.hpp"
#include <filesystem>

using hub32api::db::DatabaseManager;

TEST(DatabaseManager, OpensAndCreatesSchema)
{
    const std::string dir = "test_data_dm";
    std::filesystem::create_directories(dir);

    DatabaseManager dm(dir);
    EXPECT_TRUE(dm.isOpen());

    // school.db should exist
    EXPECT_TRUE(std::filesystem::exists(dir + "/school.db"));

    std::filesystem::remove_all(dir);
}

TEST(DatabaseManager, SchoolDbHasTables)
{
    const std::string dir = "test_data_dm2";
    std::filesystem::create_directories(dir);

    DatabaseManager dm(dir);
    auto* db = dm.schoolDb();
    ASSERT_NE(db, nullptr);

    // Check schools table exists
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT name FROM sqlite_master WHERE type='table' AND name='schools'",
        -1, &stmt, nullptr);
    EXPECT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    sqlite3_finalize(stmt);

    std::filesystem::remove_all(dir);
}
```

- [ ] **Step 2: Write DatabaseManager header**

```cpp
// src/db/DatabaseManager.hpp
#pragma once

#include <string>
#include <memory>

struct sqlite3;

namespace hub32api::db {

class DatabaseManager
{
public:
    explicit DatabaseManager(const std::string& dataDir);
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool isOpen() const noexcept;

    /// school.db handle — schools, locations, computers, teachers
    sqlite3* schoolDb() noexcept;

    /// audit.db handle — audit log (already managed by AuditLog class)
    sqlite3* auditDb() noexcept;

    /// tokens.db handle — JWT revocation (already managed by TokenStore)
    sqlite3* tokensDb() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void createSchema();
};

} // namespace hub32api::db
```

- [ ] **Step 3: Write DatabaseManager implementation**

```cpp
// src/db/DatabaseManager.cpp
#include "../core/PrecompiledHeader.hpp"
#include "DatabaseManager.hpp"
#include <sqlite3.h>
#include <filesystem>

namespace hub32api::db {

namespace {

constexpr const char* k_schoolSchema = R"(
    CREATE TABLE IF NOT EXISTS schools (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        address TEXT,
        created_at INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS locations (
        id TEXT PRIMARY KEY,
        school_id TEXT NOT NULL REFERENCES schools(id) ON DELETE CASCADE,
        name TEXT NOT NULL,
        building TEXT,
        floor INTEGER,
        capacity INTEGER,
        type TEXT DEFAULT 'classroom'
    );

    CREATE TABLE IF NOT EXISTS computers (
        id TEXT PRIMARY KEY,
        location_id TEXT REFERENCES locations(id) ON DELETE SET NULL,  -- nullable: unassigned agents have NULL
        hostname TEXT NOT NULL,
        mac_address TEXT,
        ip_last_seen TEXT,
        agent_version TEXT,
        last_heartbeat INTEGER,
        state TEXT DEFAULT 'offline',
        position_x INTEGER,
        position_y INTEGER
    );

    CREATE TABLE IF NOT EXISTS teachers (
        id TEXT PRIMARY KEY,
        username TEXT UNIQUE NOT NULL,
        password_hash TEXT NOT NULL,
        full_name TEXT NOT NULL,
        role TEXT DEFAULT 'teacher',
        created_at INTEGER NOT NULL
    );

    CREATE TABLE IF NOT EXISTS teacher_locations (
        teacher_id TEXT NOT NULL REFERENCES teachers(id) ON DELETE CASCADE,
        location_id TEXT NOT NULL REFERENCES locations(id) ON DELETE CASCADE,
        PRIMARY KEY (teacher_id, location_id)
    );

    CREATE TABLE IF NOT EXISTS active_sessions (
        computer_id TEXT PRIMARY KEY REFERENCES computers(id),
        user_login TEXT,
        user_fullname TEXT,
        session_start INTEGER,
        producer_id TEXT,
        transport_id TEXT
    );

    CREATE INDEX IF NOT EXISTS idx_computers_location ON computers(location_id);
    CREATE INDEX IF NOT EXISTS idx_computers_state ON computers(state);
    CREATE INDEX IF NOT EXISTS idx_locations_school ON locations(school_id);
    CREATE INDEX IF NOT EXISTS idx_teachers_username ON teachers(username);
)";

sqlite3* openDb(const std::string& path)
{
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) {
        spdlog::error("[DatabaseManager] cannot open {}: {}", path,
                      sqlite3_errmsg(db));
        sqlite3_close(db);
        return nullptr;
    }
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
    return db;
}

} // anonymous namespace

struct DatabaseManager::Impl
{
    sqlite3* schoolDb = nullptr;
    sqlite3* auditDb  = nullptr;
    sqlite3* tokensDb = nullptr;
    std::string dataDir;
};

DatabaseManager::DatabaseManager(const std::string& dataDir)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->dataDir = dataDir;
    std::filesystem::create_directories(dataDir);

    m_impl->schoolDb = openDb(dataDir + "/school.db");
    if (m_impl->schoolDb) {
        createSchema();
    }
    spdlog::info("[DatabaseManager] initialized at '{}'", dataDir);
}

DatabaseManager::~DatabaseManager()
{
    if (m_impl->schoolDb) sqlite3_close(m_impl->schoolDb);
    if (m_impl->auditDb)  sqlite3_close(m_impl->auditDb);
    if (m_impl->tokensDb) sqlite3_close(m_impl->tokensDb);
}

bool DatabaseManager::isOpen() const noexcept
{
    return m_impl->schoolDb != nullptr;
}

sqlite3* DatabaseManager::schoolDb() noexcept { return m_impl->schoolDb; }
sqlite3* DatabaseManager::auditDb()  noexcept { return m_impl->auditDb; }
sqlite3* DatabaseManager::tokensDb() noexcept { return m_impl->tokensDb; }

void DatabaseManager::createSchema()
{
    char* err = nullptr;
    int rc = sqlite3_exec(m_impl->schoolDb, k_schoolSchema, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        spdlog::error("[DatabaseManager] schema error: {}", err ? err : "unknown");
        sqlite3_free(err);
    }
}

} // namespace hub32api::db
```

- [ ] **Step 4: Create CMakeLists.txt for db module**

```cmake
# src/db/CMakeLists.txt
target_sources(hub32api-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/DatabaseManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SchoolRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LocationRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ComputerRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TeacherRepository.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TeacherLocationRepository.cpp
)
```

- [ ] **Step 5: Add `add_subdirectory(db)` to `src/CMakeLists.txt`**

- [ ] **Step 6: Build and run tests**

```bash
cmake --build --preset debug
ctest --test-dir build/debug -R DatabaseManager --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add src/db/ tests/unit/db/test_database_manager.cpp src/CMakeLists.txt
git commit -m "feat: add DatabaseManager with school.db schema

Creates school.db with WAL mode, foreign keys enabled.
Tables: schools, locations, computers, teachers, teacher_locations, active_sessions."
```

---

### Task 2: SchoolRepository

**Files:**
- Create: `src/db/SchoolRepository.hpp`
- Create: `src/db/SchoolRepository.cpp`
- Create: `tests/unit/db/test_school_repository.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/unit/db/test_school_repository.cpp
#include <gtest/gtest.h>
#include "db/DatabaseManager.hpp"
#include "db/SchoolRepository.hpp"
#include <filesystem>

using namespace hub32api::db;

class SchoolRepoTest : public ::testing::Test {
protected:
    std::string dir = "test_school_repo";
    std::unique_ptr<DatabaseManager> dm;
    std::unique_ptr<SchoolRepository> repo;

    void SetUp() override {
        std::filesystem::create_directories(dir);
        dm = std::make_unique<DatabaseManager>(dir);
        repo = std::make_unique<SchoolRepository>(dm->schoolDb());
    }
    void TearDown() override {
        repo.reset();
        dm.reset();
        std::filesystem::remove_all(dir);
    }
};

TEST_F(SchoolRepoTest, CreateAndFind)
{
    auto result = repo->create("Test School", "123 Main St");
    ASSERT_TRUE(result.is_ok());
    auto id = result.value();
    EXPECT_FALSE(id.empty());

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    EXPECT_EQ(findResult.value().name, "Test School");
    EXPECT_EQ(findResult.value().address, "123 Main St");
}

TEST_F(SchoolRepoTest, ListAll)
{
    repo->create("School A", "");
    repo->create("School B", "");
    auto list = repo->listAll();
    ASSERT_TRUE(list.is_ok());
    EXPECT_EQ(list.value().size(), 2u);
}

TEST_F(SchoolRepoTest, Update)
{
    auto id = repo->create("Old Name", "").value();
    auto result = repo->update(id, "New Name", "New Address");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(repo->findById(id).value().name, "New Name");
}

TEST_F(SchoolRepoTest, Delete)
{
    auto id = repo->create("To Delete", "").value();
    repo->remove(id);
    EXPECT_TRUE(repo->findById(id).is_err());
}
```

- [ ] **Step 2: Write SchoolRepository header**

```cpp
// src/db/SchoolRepository.hpp
#pragma once

#include <string>
#include <vector>
#include "hub32api/core/Result.hpp"

struct sqlite3;

namespace hub32api::db {

struct SchoolRecord {
    std::string id;
    std::string name;
    std::string address;
    int64_t     createdAt = 0;
};

class SchoolRepository
{
public:
    explicit SchoolRepository(sqlite3* db);

    Result<std::string> create(const std::string& name, const std::string& address);
    Result<SchoolRecord> findById(const std::string& id);
    Result<std::vector<SchoolRecord>> listAll();
    Result<void> update(const std::string& id, const std::string& name, const std::string& address);
    Result<void> remove(const std::string& id);

private:
    sqlite3* m_db;
};

} // namespace hub32api::db
```

- [ ] **Step 3: Write SchoolRepository implementation**

Uses prepared statements for all queries. `create()` generates a UUID via `CryptoUtils::generateUuid()`. All error paths return `Result::fail()` with appropriate error codes.

- [ ] **Step 4: Build and run tests**

```bash
cmake --build --preset debug
ctest --test-dir build/debug -R SchoolRepo --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/db/SchoolRepository.hpp src/db/SchoolRepository.cpp \
        tests/unit/db/test_school_repository.cpp
git commit -m "feat: add SchoolRepository with CRUD operations

SQLite prepared statements. UUID generation via CryptoUtils."
```

---

### Task 3: LocationRepository

**Files:**
- Create: `src/db/LocationRepository.hpp`
- Create: `src/db/LocationRepository.cpp`
- Create: `tests/unit/db/test_location_repository.cpp`

Same pattern as SchoolRepository but with:
- Foreign key to `schools(id)`
- `listBySchool(schoolId)` method
- Fields: id, school_id, name, building, floor, capacity, type

- [ ] **Step 1: Write failing tests** (create, findById, listBySchool, update, delete)
- [ ] **Step 2: Write LocationRepository header**
- [ ] **Step 3: Write LocationRepository implementation**
- [ ] **Step 4: Build and run tests**
- [ ] **Step 5: Commit**

---

### Task 4: ComputerRepository

**Files:**
- Create: `src/db/ComputerRepository.hpp`
- Create: `src/db/ComputerRepository.cpp`
- Create: `tests/unit/db/test_computer_repository.cpp`

Key methods:
- `create(locationId, hostname, macAddress)` → id
- `findById(id)` → ComputerRecord
- `listByLocation(locationId)` → vector
- `updateState(id, state)` — called from agent heartbeat
- `updateHeartbeat(id, ip, agentVersion)` — called from agent heartbeat
- `findByHostname(hostname)` — for agent registration matching
- `findByMac(mac)` — for agent registration matching
- `remove(id)`

- [ ] **Step 1: Write failing tests**
- [ ] **Step 2: Write ComputerRepository header**
- [ ] **Step 3: Write ComputerRepository implementation**
- [ ] **Step 4: Build and run tests**
- [ ] **Step 5: Commit**

---

### Task 5: TeacherRepository

**Files:**
- Create: `src/db/TeacherRepository.hpp`
- Create: `src/db/TeacherRepository.cpp`
- Create: `tests/unit/db/test_teacher_repository.cpp`

Key methods:
- `create(username, password, fullName, role)` → id (hashes password)
- `findById(id)` → TeacherRecord (without password_hash)
- `findByUsername(username)` → TeacherRecord
- `authenticate(username, password)` → Result<TeacherRecord>
- `listAll()` → vector
- `update(id, fullName, role)` → void
- `changePassword(id, oldPassword, newPassword)` → Result<void>
- `remove(id)` → void

**Integration with existing auth:** TeacherRepository replaces `UserRoleStore` for database-backed teachers. `UserRoleStore` remains as fallback for file-based users.json (bootstrap admin).

- [ ] **Step 1: Write failing tests**
- [ ] **Step 2: Write TeacherRepository header**
- [ ] **Step 3: Write TeacherRepository implementation**
- [ ] **Step 4: Build and run tests**
- [ ] **Step 5: Commit**

---

### Task 6: TeacherLocationRepository

**Files:**
- Create: `src/db/TeacherLocationRepository.hpp`
- Create: `src/db/TeacherLocationRepository.cpp`

Key methods:
- `assign(teacherId, locationId)` → Result<void>
- `revoke(teacherId, locationId)` → Result<void>
- `getLocationsForTeacher(teacherId)` → vector<LocationRecord>
- `getTeachersForLocation(locationId)` → vector<TeacherRecord>
- `hasAccess(teacherId, locationId)` → bool

- [ ] **Step 1: Write failing tests**
- [ ] **Step 2: Write header + implementation**
- [ ] **Step 3: Build and run tests**
- [ ] **Step 4: Commit**

---

### Task 7: SchoolController + TeacherController + DTOs

**Files:**
- Create: `src/api/v1/controllers/SchoolController.hpp`
- Create: `src/api/v1/controllers/SchoolController.cpp`
- Create: `src/api/v1/controllers/TeacherController.hpp`
- Create: `src/api/v1/controllers/TeacherController.cpp`
- Create: `src/api/v1/dto/SchoolDto.hpp`
- Create: `src/api/v1/dto/TeacherDto.hpp`
- Modify: `locales/en.json`, `vi.json`, `zh_CN.json` — add i18n keys

**Routes:**
```
POST   /api/v1/schools               (admin only)
GET    /api/v1/schools               (admin only)
GET    /api/v1/schools/:id           (admin only)
PUT    /api/v1/schools/:id           (admin only)
DELETE /api/v1/schools/:id           (admin only)

POST   /api/v1/locations             (admin only)
GET    /api/v1/locations             (admin: all, teacher: assigned)
GET    /api/v1/locations/:id         (admin or assigned teacher)
PUT    /api/v1/locations/:id         (admin only)
DELETE /api/v1/locations/:id         (admin only)
GET    /api/v1/locations/:id/computers  (admin or assigned teacher)

POST   /api/v1/teachers              (admin only)
GET    /api/v1/teachers              (admin only)
GET    /api/v1/teachers/:id          (admin or self)
PUT    /api/v1/teachers/:id          (admin only)
DELETE /api/v1/teachers/:id          (admin only)
POST   /api/v1/teachers/:id/locations  (admin only — assign location)
DELETE /api/v1/teachers/:id/locations/:lid  (admin only — revoke)
```

- [ ] **Step 1: Write DTOs (SchoolDto.hpp, TeacherDto.hpp)**
- [ ] **Step 2: Write SchoolController with i18n**
- [ ] **Step 3: Write TeacherController with i18n**
- [ ] **Step 4: Add i18n keys to all locale files**
- [ ] **Step 5: Build and verify**
- [ ] **Step 6: Commit**

---

### Task 8: Wire Everything into HttpServer + Router

**Files:**
- Modify: `src/server/HttpServer.cpp` — create DatabaseManager, inject repos
- Modify: `src/server/internal/Router.hpp` — add repos to Services
- Modify: `src/server/Router.cpp` — register new routes
- Modify: `include/hub32api/config/ServerConfig.hpp` — add databaseDir
- Modify: `src/config/ServerConfig.cpp` — parse databaseDir
- Modify: `conf/default.json`, `conf/development.json` — add databaseDir

- [ ] **Step 1: Add `databaseDir` to ServerConfig**

```cpp
// In ServerConfig.hpp, add:
std::string databaseDir = "data"; // default: ./data/
```

- [ ] **Step 2: Create DatabaseManager in HttpServer constructor**

```cpp
// In HttpServer::Impl, add:
std::unique_ptr<db::DatabaseManager> dbManager;
std::unique_ptr<db::SchoolRepository> schoolRepo;
std::unique_ptr<db::LocationRepository> locationRepo;
std::unique_ptr<db::ComputerRepository> computerRepo;
std::unique_ptr<db::TeacherRepository> teacherRepo;
std::unique_ptr<db::TeacherLocationRepository> teacherLocationRepo;
```

- [ ] **Step 3: Add repos to Router::Services**

```cpp
struct Services {
    // ... existing fields ...
    db::SchoolRepository& schoolRepo;
    db::LocationRepository& locationRepo;
    db::ComputerRepository& computerRepo;
    db::TeacherRepository& teacherRepo;
    db::TeacherLocationRepository& teacherLocationRepo;
};
```

- [ ] **Step 4: Register new routes in Router.cpp**

Add `registerSchoolRoutes()`, `registerTeacherRoutes()`, `registerLocationRoutes()` methods with proper auth middleware (admin-only or assigned-teacher checks).

- [ ] **Step 5: Replace ComputerPlugin mock data with ComputerRepository**

Modify `ComputerPlugin::listComputers()` to query ComputerRepository instead of NetworkDirectoryBridge mock.

- [ ] **Step 6: Build and run all tests**

```bash
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

- [ ] **Step 7: Manual integration test**

```bash
# Start server
./build/debug/bin/hub32api-service.exe --console --config conf/development.json

# Create school
curl -X POST http://127.0.0.1:11081/api/v1/schools \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"name": "Test School", "address": "123 Main St"}'

# List schools
curl http://127.0.0.1:11081/api/v1/schools \
  -H "Authorization: Bearer $TOKEN"
```

- [ ] **Step 8: Commit**

```bash
git add src/server/HttpServer.cpp src/server/Router.cpp \
        src/server/internal/Router.hpp \
        include/hub32api/config/ServerConfig.hpp \
        src/config/ServerConfig.cpp \
        src/plugins/computer/ComputerPlugin.cpp \
        src/api/v1/controllers/SchoolController.hpp src/api/v1/controllers/SchoolController.cpp \
        src/api/v1/controllers/TeacherController.hpp src/api/v1/controllers/TeacherController.cpp \
        src/api/v1/dto/SchoolDto.hpp src/api/v1/dto/TeacherDto.hpp \
        conf/default.json conf/development.json \
        locales/en.json locales/vi.json locales/zh_CN.json
git commit -m "feat: wire DatabaseManager and all repositories into server

SchoolController, TeacherController with full CRUD + role-based access.
ComputerPlugin now queries ComputerRepository instead of mock data.
Config: databaseDir field for data directory path."
```

---

## Phase 1 Completion Checklist

- [ ] school.db created with full schema (6 tables, indexes)
- [ ] SchoolRepository: CRUD with tests
- [ ] LocationRepository: CRUD + listBySchool with tests
- [ ] ComputerRepository: CRUD + state/heartbeat update with tests
- [ ] TeacherRepository: CRUD + authenticate + password change with tests
- [ ] TeacherLocationRepository: assign/revoke/hasAccess with tests
- [ ] SchoolController: 5 endpoints, admin-only, i18n
- [ ] TeacherController: 7 endpoints, admin-only + self, i18n
- [ ] LocationController: 5 endpoints, role-based access
- [ ] ComputerPlugin uses real database instead of mock
- [ ] All existing tests still pass
- [ ] All new tests pass
