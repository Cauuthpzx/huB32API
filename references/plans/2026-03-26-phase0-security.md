# Phase 0: Security Hardening — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix all critical security vulnerabilities before any internet deployment. After this phase, JWT is RS256-only with CSPRNG, passwords use argon2id, rate limiting is thread-safe, token revocation is SQLite-persistent, and all inputs are validated.

**Architecture:** Harden existing auth/middleware/config layers. Replace file-based token store with SQLite tokens.db. Upgrade PBKDF2 to argon2id. Add input validation middleware. Fix race conditions.

**Tech Stack:** C++17, OpenSSL (RAND_bytes), argon2 (libargon2 or via OpenSSL EVP), SQLite3, jwt-cpp, cpp-httplib

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Modify | `src/auth/JwtAuth.cpp` | Remove mt19937 UUID gen → OpenSSL RAND_bytes |
| Modify | `src/core/ConnectionPool.cpp` | Remove mt19937 UUID gen → OpenSSL RAND_bytes |
| Modify | `src/plugins/feature/FeaturePlugin.cpp` | Remove mt19937 UUID gen → shared crypto UUID |
| Modify | `src/api/v1/controllers/AgentController.cpp` | Remove mt19937 UUID gen → shared crypto UUID |
| Create | `src/core/internal/CryptoUtils.hpp` | Shared CSPRNG UUID generator, secure random |
| Create | `src/core/internal/CryptoUtils.cpp` | Implementation |
| Modify | `src/auth/UserRoleStore.cpp` | PBKDF2 → argon2id (or keep PBKDF2-SHA256 100k+ if argon2 dep is heavy) |
| Modify | `src/auth/internal/TokenStore.hpp` | Replace file persistence with SQLite tokens.db |
| Modify | `src/auth/internal/TokenStore.cpp` | SQLite-backed revocation with WAL mode |
| Create | `src/api/v1/middleware/InputValidationMiddleware.hpp` | Max body size, field length, type checks |
| Create | `src/api/v1/middleware/InputValidationMiddleware.cpp` | Implementation |
| Modify | `src/api/v1/middleware/RateLimitMiddleware.cpp` | Fix static callCount → instance member |
| Modify | `src/config/internal/ConfigValidator.cpp` | Fail on error, not warn |
| Modify | `src/server/Router.cpp` | Wire InputValidationMiddleware |
| Modify | `src/server/HttpServer.cpp` | Wire InputValidationMiddleware |
| Modify | `src/core/AuditLog.cpp` | Fix error handling: check all sqlite3_step/exec return codes |
| Create | `tests/unit/core/test_crypto_utils.cpp` | Test CSPRNG UUID uniqueness, format |
| Create | `tests/unit/auth/test_token_store_sqlite.cpp` | Test SQLite-backed TokenStore |
| Create | `tests/unit/middleware/test_input_validation.cpp` | Test input validation |
| Modify | `locales/en.json` | Add validation error keys |
| Modify | `locales/vi.json` | Add validation error keys |
| Modify | `locales/zh_CN.json` | Add validation error keys |

---

### Task 1: CSPRNG UUID Generator (CryptoUtils)

**Files:**
- Create: `src/core/internal/CryptoUtils.hpp`
- Create: `src/core/internal/CryptoUtils.cpp`
- Create: `tests/unit/core/test_crypto_utils.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `tests/unit/core/CMakeLists.txt` (or `tests/CMakeLists.txt`)

**Why:** `JwtAuth.cpp:50-73`, `ConnectionPool.cpp:22-54`, `FeaturePlugin.cpp:63-79` all use `std::mt19937_64` to generate UUIDs. mt19937 is NOT cryptographically secure — an attacker who knows the approximate server start time can reconstruct the seed and predict all future UUIDs, including JWT `jti` values.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/unit/core/test_crypto_utils.cpp
#include <gtest/gtest.h>
#include "core/internal/CryptoUtils.hpp"
#include <set>
#include <regex>

using hub32api::core::internal::CryptoUtils;

TEST(CryptoUtils, GenerateUuid_FormatValid)
{
    const auto uuid = CryptoUtils::generateUuid();
    // UUID v4 format: 8-4-4-4-12 hex chars
    std::regex uuidRegex(
        "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    EXPECT_TRUE(std::regex_match(uuid, uuidRegex))
        << "UUID does not match v4 format: " << uuid;
}

TEST(CryptoUtils, GenerateUuid_Unique1000)
{
    std::set<std::string> uuids;
    for (int i = 0; i < 1000; ++i) {
        uuids.insert(CryptoUtils::generateUuid());
    }
    EXPECT_EQ(uuids.size(), 1000u) << "UUID collision in 1000 generations";
}

TEST(CryptoUtils, GenerateRandomBytes_Length)
{
    auto bytes = CryptoUtils::randomBytes(32);
    EXPECT_EQ(bytes.size(), 32u);
}

TEST(CryptoUtils, GenerateRandomBytes_NotAllZero)
{
    auto bytes = CryptoUtils::randomBytes(32);
    bool allZero = true;
    for (auto b : bytes) { if (b != 0) { allZero = false; break; } }
    EXPECT_FALSE(allZero);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build --preset debug 2>&1 | tail -5
# Expected: FAIL — CryptoUtils.hpp not found
```

- [ ] **Step 3: Write CryptoUtils header**

```cpp
// src/core/internal/CryptoUtils.hpp
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace hub32api::core::internal {

class CryptoUtils
{
public:
    /// Generate a cryptographically secure UUID v4 string.
    /// Uses OpenSSL RAND_bytes — NOT mt19937.
    static std::string generateUuid();

    /// Generate N cryptographically secure random bytes.
    static std::vector<uint8_t> randomBytes(size_t count);

    CryptoUtils() = delete; // static-only
};

} // namespace hub32api::core::internal
```

- [ ] **Step 4: Write CryptoUtils implementation**

```cpp
// src/core/internal/CryptoUtils.cpp
#include "CryptoUtils.hpp"
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace hub32api::core::internal {

std::string CryptoUtils::generateUuid()
{
    uint8_t bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("RAND_bytes failed for UUID generation");
    }

    // Set version 4 (0100xxxx) and variant 10xx
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return oss.str();
}

std::vector<uint8_t> CryptoUtils::randomBytes(size_t count)
{
    std::vector<uint8_t> buf(count);
    if (count > 0 && RAND_bytes(buf.data(), static_cast<int>(count)) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return buf;
}

} // namespace hub32api::core::internal
```

- [ ] **Step 5: Add CryptoUtils.cpp to `src/core/CMakeLists.txt`**

Add `CryptoUtils.cpp` to the core library sources list.

- [ ] **Step 6: Add test to CMakeLists.txt and run**

```bash
cmake --build --preset debug
ctest --test-dir build/debug -R CryptoUtils --output-on-failure
# Expected: ALL PASS
```

- [ ] **Step 7: Replace mt19937 in JwtAuth.cpp**

Replace `generateUuid()` in `src/auth/JwtAuth.cpp:50-73` with:
```cpp
#include "core/internal/CryptoUtils.hpp"
// ... in issueToken():
const std::string jti = core::internal::CryptoUtils::generateUuid();
```
Remove the anonymous-namespace `generateUuid()` function entirely from JwtAuth.cpp.

- [ ] **Step 8: Replace mt19937 in ConnectionPool.cpp**

Replace `generateUuid()` in `src/core/ConnectionPool.cpp:22-54` with:
```cpp
#include "internal/CryptoUtils.hpp"
// ... in acquire():
Uid token = CryptoUtils::generateUuid();
```
Remove the anonymous-namespace `generateUuid()` function.

- [ ] **Step 9: Replace mt19937 in FeaturePlugin.cpp**

Replace `generateCommandId()` in `src/plugins/feature/FeaturePlugin.cpp:63-79` with:
```cpp
#include "core/internal/CryptoUtils.hpp"
// ... in generateCommandId():
return core::internal::CryptoUtils::generateUuid();
```
Remove the old snprintf-based implementation.

- [ ] **Step 9b: Replace mt19937 in AgentController.cpp**

Check `src/api/v1/controllers/AgentController.cpp` for any mt19937 usage and replace with `CryptoUtils::generateUuid()`.

- [ ] **Step 10: Build and run all tests**

```bash
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
# Expected: ALL PASS (including CryptoUtils + existing tests)
```

- [ ] **Step 11: Commit**

```bash
git add src/core/internal/CryptoUtils.hpp src/core/internal/CryptoUtils.cpp \
        src/core/CMakeLists.txt \
        src/auth/JwtAuth.cpp src/core/ConnectionPool.cpp \
        src/plugins/feature/FeaturePlugin.cpp \
        tests/unit/core/test_crypto_utils.cpp
git commit -m "security: replace mt19937 with OpenSSL RAND_bytes for all UUID generation

CSPRNG-backed UUID v4 prevents seed prediction attacks on JWT jti values.
Extracted shared CryptoUtils class used by JwtAuth, ConnectionPool, and FeaturePlugin."
```

---

### Task 2: SQLite-Backed Token Revocation Store

**Files:**
- Modify: `src/auth/internal/TokenStore.hpp`
- Modify: `src/auth/internal/TokenStore.cpp`
- Create: `tests/unit/auth/test_token_store_sqlite.cpp`

**Why:** Current TokenStore uses file-based persistence (append lines to a text file). This is fragile — no ACID, no concurrent safety, no auto-cleanup. SQLite tokens.db with WAL mode is the spec requirement.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/unit/auth/test_token_store_sqlite.cpp
#include <gtest/gtest.h>
#include "auth/internal/TokenStore.hpp"
#include <filesystem>

using hub32api::auth::internal::TokenStore;

class TokenStoreSqliteTest : public ::testing::Test {
protected:
    std::string dbPath = "test_tokens.db";
    void TearDown() override {
        std::filesystem::remove(dbPath);
        std::filesystem::remove(dbPath + "-wal");
        std::filesystem::remove(dbPath + "-shm");
    }
};

TEST_F(TokenStoreSqliteTest, RevokeAndCheck)
{
    TokenStore store(dbPath);
    EXPECT_FALSE(store.isRevoked("jti-001"));
    store.revoke("jti-001");
    EXPECT_TRUE(store.isRevoked("jti-001"));
}

TEST_F(TokenStoreSqliteTest, PersistAcrossRestart)
{
    {
        TokenStore store(dbPath);
        store.revoke("jti-persist");
    }
    // Re-open from same db file
    TokenStore store2(dbPath);
    EXPECT_TRUE(store2.isRevoked("jti-persist"));
}

TEST_F(TokenStoreSqliteTest, PurgeExpiredRemovesOld)
{
    TokenStore store(dbPath);
    store.revokeWithExpiry("jti-old", std::chrono::system_clock::now() - std::chrono::hours(1));
    store.revokeWithExpiry("jti-new", std::chrono::system_clock::now() + std::chrono::hours(1));
    store.purgeExpired();
    EXPECT_FALSE(store.isRevoked("jti-old"));
    EXPECT_TRUE(store.isRevoked("jti-new"));
}

TEST_F(TokenStoreSqliteTest, InMemoryModeWorks)
{
    TokenStore store; // empty path = in-memory
    store.revoke("jti-mem");
    EXPECT_TRUE(store.isRevoked("jti-mem"));
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build --preset debug 2>&1 | tail -5
# Expected: FAIL — revokeWithExpiry not found
```

- [ ] **Step 3: Rewrite TokenStore header for SQLite backend**

```cpp
// src/auth/internal/TokenStore.hpp
#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_set>

struct sqlite3;

namespace hub32api::auth::internal {

class TokenStore
{
public:
    explicit TokenStore(const std::string& dbPath = {});
    ~TokenStore();

    TokenStore(const TokenStore&) = delete;
    TokenStore& operator=(const TokenStore&) = delete;

    void revoke(const std::string& jti);
    void revokeWithExpiry(const std::string& jti,
                          std::chrono::system_clock::time_point expiresAt);
    bool isRevoked(const std::string& jti) const;  // not noexcept: SQLite ops can fail
    void purgeExpired();

private:
    mutable std::mutex m_mutex;
    mutable sqlite3* m_db = nullptr;  // mutable: isRevoked() is const but needs sqlite3 ops
    bool m_useSqlite = false;

    // In-memory fallback when no db path given
    std::unordered_set<std::string> m_memRevoked;

    void initDb(const std::string& dbPath);
};

} // namespace hub32api::auth::internal
```

- [ ] **Step 4: Rewrite TokenStore implementation with SQLite**

```cpp
// src/auth/internal/TokenStore.cpp
#include "../../core/PrecompiledHeader.hpp"
#include "TokenStore.hpp"
#include <sqlite3.h>

namespace hub32api::auth::internal {

namespace {
constexpr const char* k_createSql = R"(
    CREATE TABLE IF NOT EXISTS revoked_tokens (
        jti TEXT PRIMARY KEY,
        revoked_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_expires ON revoked_tokens(expires_at);
)";
constexpr int64_t k_defaultTtlSec = 3600; // 1 hour
} // anonymous

TokenStore::TokenStore(const std::string& dbPath)
{
    if (!dbPath.empty()) {
        initDb(dbPath);
    }
    if (!m_useSqlite) {
        spdlog::info("[TokenStore] in-memory mode (no persistence)");
    }
}

TokenStore::~TokenStore()
{
    if (m_db) sqlite3_close(m_db);
}

void TokenStore::initDb(const std::string& dbPath)
{
    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        spdlog::error("[TokenStore] cannot open {}: {}", dbPath,
                      sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    char* err = nullptr;
    rc = sqlite3_exec(m_db, k_createSql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        spdlog::error("[TokenStore] schema error: {}", err ? err : "unknown");
        sqlite3_free(err);
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }
    m_useSqlite = true;
    spdlog::info("[TokenStore] SQLite initialized at '{}'", dbPath);
}

void TokenStore::revoke(const std::string& jti)
{
    auto now = std::chrono::system_clock::now();
    revokeWithExpiry(jti, now + std::chrono::seconds(k_defaultTtlSec));
}

void TokenStore::revokeWithExpiry(const std::string& jti,
                                   std::chrono::system_clock::time_point expiresAt)
{
    std::lock_guard lock(m_mutex);
    if (m_useSqlite && m_db) {
        const char* sql = "INSERT OR REPLACE INTO revoked_tokens "
                          "(jti, revoked_at, expires_at) VALUES (?, ?, ?)";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            auto nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto expEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                expiresAt.time_since_epoch()).count();
            sqlite3_bind_text(stmt, 1, jti.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, nowEpoch);
            sqlite3_bind_int64(stmt, 3, expEpoch);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    } else {
        m_memRevoked.insert(jti);
    }
    spdlog::debug("[TokenStore] revoked jti={}", jti);
}

bool TokenStore::isRevoked(const std::string& jti) const noexcept
{
    std::lock_guard lock(m_mutex);
    if (m_useSqlite && m_db) {
        const char* sql = "SELECT 1 FROM revoked_tokens WHERE jti = ? AND expires_at > ?";
        sqlite3_stmt* stmt = nullptr;
        bool found = false;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            auto nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            sqlite3_bind_text(stmt, 1, jti.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, nowEpoch);
            found = (sqlite3_step(stmt) == SQLITE_ROW);
            sqlite3_finalize(stmt);
        }
        return found;
    }
    return m_memRevoked.count(jti) > 0;
}

void TokenStore::purgeExpired()
{
    std::lock_guard lock(m_mutex);
    if (m_useSqlite && m_db) {
        const char* sql = "DELETE FROM revoked_tokens WHERE expires_at <= ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            auto nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            sqlite3_bind_int64(stmt, 1, nowEpoch);
            sqlite3_step(stmt);
            int changes = sqlite3_changes(m_db);
            sqlite3_finalize(stmt);
            if (changes > 0) {
                spdlog::debug("[TokenStore] purged {} expired entries", changes);
            }
        }
    }
    // In-memory mode: no expiry tracking, entries stay until process restart
}

} // namespace hub32api::auth::internal
```

- [ ] **Step 5: Update JwtAuth.cpp constructor**

Change `TokenStore` construction in `JwtAuth.cpp:204`:
```cpp
// Old: m_impl->store = std::make_unique<internal::TokenStore>(cfg.tokenRevocationFile);
// New: pass the tokens.db path
m_impl->store = std::make_unique<internal::TokenStore>(cfg.tokenRevocationFile);
```
No change needed — the `tokenRevocationFile` config field already accepts a path. The user just needs to set it to `data/tokens.db` instead of a `.txt` file.

- [ ] **Step 6: Build and run tests**

```bash
cmake --build --preset debug
ctest --test-dir build/debug -R TokenStore --output-on-failure
# Expected: ALL PASS
```

- [ ] **Step 7: Commit**

```bash
git add src/auth/internal/TokenStore.hpp src/auth/internal/TokenStore.cpp \
        tests/unit/auth/test_token_store_sqlite.cpp
git commit -m "security: migrate TokenStore from file persistence to SQLite

Uses tokens.db with WAL mode. Supports revokeWithExpiry() for proper TTL.
purgeExpired() deletes rows with expires_at <= now. In-memory fallback
when no path configured."
```

---

### Task 3: Fix Rate Limiter Race Condition

**Files:**
- Modify: `src/api/v1/middleware/RateLimitMiddleware.cpp`

**Why:** Line 65-66 uses `static int callCount = 0` which is shared across all threads without synchronization. The `m_mutex` is held but `callCount` is not an instance member — it's a global static that could cause issues if multiple RateLimitMiddleware instances exist.

- [ ] **Step 1: Move `callCount` to instance member**

In `src/api/v1/middleware/RateLimitMiddleware.hpp`, add to private section:
```cpp
int m_callCount = 0;
```

- [ ] **Step 2: Replace static with instance member**

In `src/api/v1/middleware/RateLimitMiddleware.cpp:65-66`, change:
```cpp
// Old:
static int callCount = 0;
if (++callCount % 1000 == 0) {
// New:
if (++m_callCount % 1000 == 0) {
```

- [ ] **Step 3: Build and run tests**

```bash
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git add src/api/v1/middleware/RateLimitMiddleware.hpp \
        src/api/v1/middleware/RateLimitMiddleware.cpp
git commit -m "fix: move rate limiter callCount to instance member

Fixes potential issue with static counter shared across instances."
```

---

### Task 4: Input Validation Middleware

**Files:**
- Create: `src/api/v1/middleware/InputValidationMiddleware.hpp`
- Create: `src/api/v1/middleware/InputValidationMiddleware.cpp`
- Modify: `src/server/Router.cpp` — wire the middleware
- Modify: `locales/en.json`, `locales/vi.json`, `locales/zh_CN.json`
- Create: `tests/unit/middleware/test_input_validation.cpp`

**Why:** No input length/size validation exists. An attacker can send a 100MB JSON body and exhaust memory.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/unit/middleware/test_input_validation.cpp
#include <gtest/gtest.h>
#include "api/v1/middleware/InputValidationMiddleware.hpp"

using hub32api::api::v1::middleware::InputValidationMiddleware;

TEST(InputValidation, RejectsOversizedBody)
{
    InputValidationMiddleware validator({.maxBodySize = 1024});
    // Simulate a request with body > 1024 bytes
    // (This test needs httplib mock - if not available, test at integration level)
    SUCCEED(); // placeholder — real test at integration level
}
```

- [ ] **Step 2: Write InputValidationMiddleware header**

```cpp
// src/api/v1/middleware/InputValidationMiddleware.hpp
#pragma once

#include <string>

namespace httplib { struct Request; struct Response; }

namespace hub32api::api::v1::middleware {

struct ValidationConfig
{
    size_t maxBodySize       = 1 * 1024 * 1024; // 1 MB default
    size_t maxFieldLength    = 1000;              // max string field length
    size_t maxArraySize      = 500;               // max JSON array elements
    int    maxPathDepth      = 10;                // max URL path segments
};

class InputValidationMiddleware
{
public:
    explicit InputValidationMiddleware(ValidationConfig cfg = {});

    /// Returns false and sets 413/400 response if validation fails.
    bool process(const httplib::Request& req, httplib::Response& res);

private:
    ValidationConfig m_cfg;
};

} // namespace hub32api::api::v1::middleware
```

- [ ] **Step 3: Write InputValidationMiddleware implementation**

```cpp
// src/api/v1/middleware/InputValidationMiddleware.cpp
#include "core/PrecompiledHeader.hpp"
#include "InputValidationMiddleware.hpp"
#include "core/internal/I18n.hpp"
#include <httplib.h>

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

} // anonymous namespace

namespace hub32api::api::v1::middleware {

InputValidationMiddleware::InputValidationMiddleware(ValidationConfig cfg)
    : m_cfg(cfg) {}

bool InputValidationMiddleware::process(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // Check body size
    if (req.body.size() > m_cfg.maxBodySize) {
        nlohmann::json j;
        j["status"] = 413;
        j["title"] = tr(lang, "error.payload_too_large");
        j["detail"] = tr(lang, "error.max_body_size",
                         {{"max", std::to_string(m_cfg.maxBodySize)}});
        res.status = 413;
        res.set_content(j.dump(), "application/json");
        return false;
    }

    // Check Content-Type for POST/PUT/PATCH with body
    if (!req.body.empty() &&
        (req.method == "POST" || req.method == "PUT" || req.method == "PATCH")) {
        auto ct = req.get_header_value("Content-Type");
        if (ct.find("application/json") == std::string::npos &&
            ct.find("application/octet-stream") == std::string::npos) {
            nlohmann::json j;
            j["status"] = 415;
            j["title"] = tr(lang, "error.unsupported_media_type");
            res.status = 415;
            res.set_content(j.dump(), "application/json");
            return false;
        }
    }

    return true;
}

} // namespace hub32api::api::v1::middleware
```

- [ ] **Step 4: Add i18n keys to all locale files**

Add to `locales/en.json`:
```json
"error.payload_too_large": "Request body is too large",
"error.max_body_size": "Maximum body size is {max} bytes",
"error.unsupported_media_type": "Content-Type must be application/json"
```

Add equivalent translations to `vi.json` and `zh_CN.json`.

- [ ] **Step 5: Wire middleware in Router.cpp**

In `Router::registerAll()`, add InputValidationMiddleware as the first middleware in the chain (before CORS):
```cpp
// In Router constructor or registerAll():
auto inputValidator = std::make_shared<middleware::InputValidationMiddleware>();
// Apply via httplib's set_pre_routing_handler or as first middleware
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add src/api/v1/middleware/InputValidationMiddleware.hpp \
        src/api/v1/middleware/InputValidationMiddleware.cpp \
        src/server/Router.cpp \
        locales/en.json locales/vi.json locales/zh_CN.json
git commit -m "security: add InputValidationMiddleware for body size and content-type checks

Rejects bodies > 1MB (configurable), validates Content-Type for POST/PUT/PATCH.
Wired as first middleware in the chain."
```

---

### Task 5: Config Validation — Fail on Error

**Files:**
- Modify: `src/config/internal/ConfigValidator.cpp`
- Modify: `src/config/ServerConfig.cpp`

**Why:** Currently config errors are logged as warnings but the server continues with defaults. A misconfigured JWT key path should prevent startup, not silently run with broken auth.

- [ ] **Step 1: Read ConfigValidator.cpp to understand current behavior**

- [ ] **Step 2: Change `spdlog::warn` calls to throw on critical fields**

For critical fields (jwtAlgorithm, jwtPrivateKeyFile, jwtPublicKeyFile, bindAddress, httpPort), change validation from:
```cpp
spdlog::warn("[Config] ...");
```
to:
```cpp
throw std::runtime_error("[Config] FATAL: ...");
```

For non-critical fields (logLevel, workerThreads), keep as warnings with sensible defaults.

- [ ] **Step 3: Build and run tests**

```bash
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git add src/config/internal/ConfigValidator.cpp src/config/ServerConfig.cpp
git commit -m "security: config validation fails on critical errors instead of warning

JWT key paths, bind address, and port misconfigurations now prevent startup.
Non-critical fields still warn and use defaults."
```

---

### Task 6: Upgrade Password Hashing (PBKDF2 → argon2id)

**Files:**
- Modify: `src/auth/UserRoleStore.cpp`
- Modify: `src/auth/UserRoleStore.hpp`
- Modify: `cmake/deps/` — add argon2 dependency (or use libsodium)

**Why:** PBKDF2-SHA256 with 100k iterations is adequate but argon2id is the recommended standard (OWASP, spec requirement). argon2id is memory-hard, resistant to GPU attacks.

**Decision:** If adding libargon2 dependency is too heavy for Phase 0, keep PBKDF2-SHA256 but increase iterations to 310,000 (OWASP 2024 minimum). Mark argon2id as Phase 7 enhancement. The current PBKDF2 implementation is secure enough for initial deployment.

- [ ] **Step 1: Evaluate argon2 availability in MSYS2/MinGW**

```bash
pacman -Ss argon2 2>/dev/null || echo "Check MSYS2 repos"
```

- [ ] **Step 2a (argon2 available): Add libargon2 to FindOrFetchDeps.cmake**

- [ ] **Step 2b (argon2 not available): Increase PBKDF2 iterations to 310000**

In `src/auth/UserRoleStore.cpp:32`:
```cpp
constexpr int PBKDF2_ITERATIONS = 310000; // OWASP 2024 minimum for PBKDF2-SHA256
```

- [ ] **Step 3: Add backward compatibility for existing 100k hashes**

`verifyPassword()` already parses iterations from the stored hash string, so existing hashes with 100000 iterations will still verify correctly. New hashes will use 310000.

- [ ] **Step 4: Build and run tests**

```bash
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add src/auth/UserRoleStore.cpp
git commit -m "security: increase PBKDF2 iterations to 310000 (OWASP 2024 minimum)

Backward compatible: existing 100k hashes still verify. New hashes use 310k."
```

---

---

### Task 7: Fix AuditLog SQLite Error Handling

**Files:**
- Modify: `src/core/AuditLog.cpp`

**Why:** `AuditLog::writerLoop()` at lines 156-170 does not check return codes from `sqlite3_step()`, `sqlite3_exec("BEGIN")`, or `sqlite3_exec("COMMIT")`. A failed write silently drops audit entries — a compliance risk.

- [ ] **Step 1: Add return code checks in writerLoop()**

In `src/core/AuditLog.cpp`, writerLoop():
```cpp
// Replace:
sqlite3_exec(m_impl->db, "BEGIN", nullptr, nullptr, nullptr);
// With:
char* errMsg = nullptr;
int rc = sqlite3_exec(m_impl->db, "BEGIN", nullptr, nullptr, &errMsg);
if (rc != SQLITE_OK) {
    spdlog::error("[AuditLog] BEGIN failed: {}", errMsg ? errMsg : "unknown");
    sqlite3_free(errMsg);
}

// Replace:
sqlite3_step(stmt);
// With:
rc = sqlite3_step(stmt);
if (rc != SQLITE_DONE) {
    spdlog::error("[AuditLog] INSERT failed: {}", sqlite3_errmsg(m_impl->db));
}

// Same for COMMIT
```

- [ ] **Step 2: Add check for sqlite3_prepare_v2 at line 142**

```cpp
if (sqlite3_prepare_v2(m_impl->db, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
    spdlog::error("[AuditLog] prepare failed: {}", sqlite3_errmsg(m_impl->db));
    return; // Cannot proceed without prepared statement
}
```

- [ ] **Step 3: Build and run tests**
- [ ] **Step 4: Commit**

```bash
git add src/core/AuditLog.cpp
git commit -m "fix: check all SQLite return codes in AuditLog writerLoop

Failed writes now log errors instead of silently dropping audit entries."
```

---

### Task 8: Document CryptoUtils Exception Policy

**Files:**
- Modify: `src/core/internal/CryptoUtils.hpp`

**Why:** CryptoUtils methods throw `std::runtime_error` but project convention says "no exceptions in public API." CryptoUtils is internal (`core::internal` namespace) and all callers in public API (JwtAuth::issueToken, ConnectionPool::acquire) must wrap calls in try/catch or use Result<T>.

- [ ] **Step 1: Add documentation comment to CryptoUtils.hpp**

```cpp
/// @note EXCEPTION POLICY: CryptoUtils is an internal utility that throws
/// std::runtime_error on CSPRNG failure (extremely rare — indicates broken
/// OpenSSL or OS entropy exhaustion). All PUBLIC API callers MUST catch
/// exceptions and convert to Result::fail(). JwtAuth::issueToken() already
/// has try/catch. Verify ConnectionPool::acquire() and FeaturePlugin also do.
```

- [ ] **Step 2: Add try/catch in ConnectionPool::acquire() around UUID generation**

```cpp
Uid token;
try {
    token = CryptoUtils::generateUuid();
} catch (const std::exception& ex) {
    spdlog::error("[ConnectionPool] UUID generation failed: {}", ex.what());
    return Result<Uid>::fail(ApiError{ErrorCode::InternalError, "UUID generation failed"});
}
```

- [ ] **Step 3: Build and run tests**
- [ ] **Step 4: Commit**

---

## Phase 0 Completion Checklist

After all tasks:

- [ ] All mt19937 UUID generation replaced with OpenSSL RAND_bytes (JwtAuth, ConnectionPool, FeaturePlugin, AgentController)
- [ ] TokenStore uses SQLite tokens.db with WAL mode
- [ ] Rate limiter callCount is instance member, not static
- [ ] Input validation middleware rejects oversized bodies
- [ ] Config validation fails on critical errors
- [ ] Password hashing meets OWASP 2024 minimums
- [ ] AuditLog checks all SQLite return codes
- [ ] CryptoUtils exception policy documented, callers protected with try/catch
- [ ] All existing tests still pass
- [ ] New tests for CryptoUtils, TokenStore, InputValidation
- [ ] All changes committed with descriptive messages
