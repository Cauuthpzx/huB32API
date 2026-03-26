# Phase 0: Critical Security Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harden hub32api with 8 critical security fixes: remove HS256, enforce config validation, mandatory TLS, persistent token revocation, SQLite error handling, input validation, Argon2id, per-endpoint rate limiting.

**Architecture:** Each fix is a self-contained commit touching 2-6 files. Execution order respects dependencies: HS256 removal first (highest risk), then config fail-on-error (unblocks TLS and token store changes), then independent items.

**Tech Stack:** C++17, CMake 3.20+, OpenSSL, SQLite3, jwt-cpp, libargon2 (FetchContent), httplib, GoogleTest

**Build command:** `cd /c/Users/Admin/Desktop/veyon/hub32api && PATH="/c/msys64/mingw64/bin:$PATH" cmake --build build/debug 2>&1 | tail -30`

**Test command:** `cd /c/Users/Admin/Desktop/veyon/hub32api/build/debug && PATH="/c/msys64/mingw64/bin:$PATH" ctest --output-on-failure 2>&1`

**Commit format:** `fix(security): description`

---

## File Structure

### Files to modify:
- `include/hub32api/core/Constants.hpp` — add kRsaMinKeyBits, Argon2 constants, kMaxQueryParamLength, kAuthRateRequestsPerMinute
- `src/core/Constants.cpp` — simplify JwtAlgorithm to_string (RS256 only)
- `include/hub32api/config/ServerConfig.hpp` — change from_file/from_registry to return Result<ServerConfig>
- `src/config/ServerConfig.cpp` — propagate errors instead of fallback to defaults, remove generateRandomSecret
- `src/config/internal/ConfigValidator.cpp` — remove HS256, add TLS critical check
- `src/auth/JwtAuth.hpp` — remove secret param references
- `src/auth/JwtAuth.cpp` — remove HS256 code path
- `src/auth/internal/JwtValidator.hpp` — remove secret param
- `src/auth/internal/JwtValidator.cpp` — remove HS256 verification
- `src/auth/internal/TokenStore.hpp` — factory pattern, remove silent fallback
- `src/auth/internal/TokenStore.cpp` — fail on DB error when path configured
- `src/auth/UserRoleStore.cpp` — add Argon2id, keep PBKDF2 verification
- `src/auth/UserRoleStore.hpp` — update comments
- `src/server/HttpServer.cpp` — handle Result<ServerConfig>, TLS validation
- `src/service/main.cpp` — handle Result<ServerConfig>
- `src/core/AuditLog.cpp` — check all SQLite return codes, ROLLBACK on failure
- `src/api/v1/middleware/InputValidationMiddleware.cpp` — depth check before recursion
- `src/api/v1/middleware/RateLimitMiddleware.hpp` — per-endpoint key
- `src/api/v1/middleware/RateLimitMiddleware.cpp` — per-endpoint key, document clocks
- `src/api/v1/controllers/ComputerController.cpp` — use safe_stoi
- `src/api/v1/controllers/FramebufferController.cpp` — use safe_stoi
- `src/utils/string_utils.hpp` — add safe_stoi, safe_stod
- `src/utils/string_utils.cpp` — implement safe_stoi, safe_stod

### Files to create:
- `cmake/deps/FindArgon2.cmake` — FetchContent for libargon2
- `tests/unit/auth/test_hs256_rejected.cpp` — verify HS256 is rejected
- `tests/unit/auth/test_argon2id.cpp` — Argon2id hash/verify roundtrip

---

## Task 1: Remove HS256 — RS256 only

**Files:**
- Modify: `include/hub32api/core/Constants.hpp`
- Modify: `src/core/Constants.cpp`
- Modify: `src/auth/JwtAuth.cpp`
- Modify: `src/auth/internal/JwtValidator.hpp`
- Modify: `src/auth/internal/JwtValidator.cpp`
- Modify: `src/config/internal/ConfigValidator.cpp`
- Modify: `src/config/ServerConfig.cpp`

- [ ] **Step 1: Update Constants.hpp — remove HS256 from enum**

In `include/hub32api/core/Constants.hpp`:

Replace the `JwtAlgorithm` enum and related declarations:
```cpp
// -----------------------------------------------------------------------
// JwtAlgorithm — supported JWT signing algorithms
// -----------------------------------------------------------------------
enum class JwtAlgorithm
{
    RS256,   // RSA-SHA256 — the ONLY supported algorithm
};

HUB32API_EXPORT std::string to_string(JwtAlgorithm alg);
```

Remove the `jwt_algorithm_from_string()` declaration entirely.

Add after the crypto constants section:
```cpp
constexpr int kRsaMinKeyBits = 4096;  // bits — minimum RSA key size
```

- [ ] **Step 2: Update Constants.cpp — simplify JwtAlgorithm**

In `src/core/Constants.cpp`, replace the JwtAlgorithm functions:

```cpp
// -- JwtAlgorithm -----------------------------------------------------------
std::string to_string(JwtAlgorithm alg)
{
    switch (alg) {
        case JwtAlgorithm::RS256: return "RS256";
    }
    return "RS256";
}
```

Remove `jwt_algorithm_from_string()` implementation entirely.

- [ ] **Step 3: Update ConfigValidator — reject HS256**

In `src/config/internal/ConfigValidator.cpp`, replace the JWT algorithm check:

```cpp
    // --- CRITICAL: JWT algorithm must be RS256 ---
    if (cfg.jwtAlgorithm != to_string(JwtAlgorithm::RS256)) {
        return Result<std::vector<std::string>>::fail(ApiError{
            ErrorCode::InvalidConfig,
            "jwtAlgorithm must be \"RS256\" (got \"" + cfg.jwtAlgorithm + "\"). HS256 is no longer supported."
        });
    }
```

- [ ] **Step 4: Update JwtAuth::create() — remove HS256 path**

In `src/auth/JwtAuth.cpp`, in the `create()` factory:

1. Remove the HS256 secret check block:
```cpp
// DELETE this block:
    if (cfg.jwtAlgorithm == to_string(JwtAlgorithm::HS256) && cfg.jwtSecret.empty()) {
        ...
    }
```

2. Change the initial algorithm check to reject anything except RS256:
```cpp
    if (cfg.jwtAlgorithm != to_string(JwtAlgorithm::RS256)) {
        return Result<std::unique_ptr<JwtAuth>>::fail(ApiError{
            ErrorCode::InvalidConfig,
            "[JwtAuth] Only RS256 algorithm is supported. HS256 has been removed."
        });
    }
```

3. Remove the `if (cfg.jwtAlgorithm == to_string(JwtAlgorithm::RS256))` condition — the RS256 key loading is now unconditional (always required).

4. In `issueToken()`, remove the HS256 signing branch. Only keep RS256:
```cpp
        std::string token = builder.sign(
            jwt::algorithm::rs256{m_impl->publicKey, m_impl->privateKey, "", ""});
```

5. Remove `m_impl->secret` usage for signing (keep the field in Impl for backwards compat but don't read it).

- [ ] **Step 5: Update JwtValidator — remove HS256 verification**

In `src/auth/internal/JwtValidator.hpp`, simplify constructor:
```cpp
    explicit JwtValidator(const std::string& publicKey);
```

In `src/auth/internal/JwtValidator.cpp`:
1. Remove `m_secret` member and `secret` constructor param
2. In `validate()`, remove the HS256 algorithm branch — only RS256:
```cpp
        verifier.allow_algorithm(jwt::algorithm::rs256{m_publicKey, "", "", ""});
```
3. Keep the algorithm pinning check but only accept RS256.

- [ ] **Step 6: Update ServerConfig — remove generateRandomSecret**

In `src/config/ServerConfig.cpp`:
1. Remove `generateRandomSecret()` function entirely
2. Remove all `if (cfg.jwtSecret.empty()) { ... generateRandomSecret ... }` blocks from `from_file()`, `from_registry()`, and `defaults()`
3. If `jwtSecret` is present in config JSON, log a deprecation warning:
```cpp
    if (j.contains("jwtSecret") && !j["jwtSecret"].get<std::string>().empty()) {
        spdlog::warn("[ServerConfig] jwtSecret is deprecated — HS256 is no longer supported. Use RS256 with key files.");
    }
```

- [ ] **Step 7: Fix all compilation errors**

Build and fix any callers that reference `jwt_algorithm_from_string()`, `JwtAlgorithm::HS256`, or the old JwtValidator constructor signature. Iterate until build passes.

- [ ] **Step 8: Update tests**

Update `tests/unit/auth/test_jwt_auth.cpp`:
- Remove all HS256 test cases
- Add test: `create()` with algorithm="HS256" returns error
- Existing RS256 tests should still pass

- [ ] **Step 9: Build and test**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && PATH="/c/msys64/mingw64/bin:$PATH" cmake --build build/debug 2>&1 | tail -20
cd /c/Users/Admin/Desktop/veyon/hub32api/build/debug && PATH="/c/msys64/mingw64/bin:$PATH" ctest --output-on-failure
```

- [ ] **Step 10: Commit**

```bash
git add -A && git commit -m "fix(security): remove HS256 support — RS256 only with RSA keys

HS256 has been completely removed. The server now requires RS256
with PEM key files. JwtAlgorithm enum reduced to RS256 only.
jwt_algorithm_from_string() removed. JwtValidator simplified to
only accept RS256 public key. generateRandomSecret() removed.
HS256 config triggers deprecation warning."
```

---

## Task 2: Config fail-on-error

**Files:**
- Modify: `include/hub32api/config/ServerConfig.hpp`
- Modify: `src/config/ServerConfig.cpp`
- Modify: `src/service/main.cpp`
- Modify: `src/server/HttpServer.cpp`

- [ ] **Step 1: Change ServerConfig return types to Result**

In `include/hub32api/config/ServerConfig.hpp`, change:
```cpp
    HUB32API_EXPORT static Result<ServerConfig> from_file(const std::string& path);
    HUB32API_EXPORT static Result<ServerConfig> from_registry();
    HUB32API_EXPORT static ServerConfig defaults();  // unchanged — defaults are always valid
```

Add `#include "hub32api/core/Result.hpp"` if not present.

- [ ] **Step 2: Update ServerConfig.cpp — propagate errors**

In `src/config/ServerConfig.cpp`:

`from_file()`:
- On JSON parse error: return `Result::fail()` instead of `defaults()`
- On critical validation error: return `Result::fail()` instead of `defaults()`
- On success: `return Result<ServerConfig>::ok(std::move(cfg));`

`from_registry()`:
- On registry open failure: return `Result::fail()` instead of `defaults()`
- On critical validation error: return `Result::fail()` instead of `defaults()`
- On success: `return Result<ServerConfig>::ok(std::move(cfg));`

- [ ] **Step 3: Update main.cpp — handle Result**

In `src/service/main.cpp`, replace:
```cpp
    auto cfg = configPath.empty()
        ? hub32api::ServerConfig::from_registry()
        : hub32api::ServerConfig::from_file(configPath);
```
with:
```cpp
    auto cfgResult = configPath.empty()
        ? hub32api::ServerConfig::from_registry()
        : hub32api::ServerConfig::from_file(configPath);

    if (cfgResult.is_err()) {
        spdlog::critical("[main] configuration error: {}", cfgResult.error().message);
        return 1;
    }
    auto cfg = cfgResult.take();
```

- [ ] **Step 4: Update HttpServer — validate TLS at construction**

In `src/server/HttpServer.cpp` constructor, add after line 104 (`m_impl->cfg = cfg;`):
```cpp
    // Validate TLS configuration at startup
    if (cfg.tlsEnabled) {
        if (cfg.tlsCertFile.empty() || cfg.tlsKeyFile.empty()) {
            throw std::runtime_error(
                "[HttpServer] TLS is enabled but tlsCertFile or tlsKeyFile is empty. "
                "Provide valid certificate and key files or disable TLS.");
        }
    }
```

- [ ] **Step 5: Fix all callers**

Build and fix all callers of `from_file()` / `from_registry()` that don't handle Result. This includes tests and examples.

- [ ] **Step 6: Build and test**

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "fix(security): config fail-on-error — server refuses to start with invalid config

ServerConfig::from_file() and from_registry() now return Result<ServerConfig>.
Critical config errors propagate instead of silently falling back to defaults.
main.cpp exits with code 1 on config error. HttpServer validates TLS config."
```

---

## Task 3: TLS mandatory when enabled

**Files:**
- Modify: `src/config/internal/ConfigValidator.cpp`
- Modify: `src/server/HttpServer.cpp`

- [ ] **Step 1: Move TLS validation to critical in ConfigValidator**

In `src/config/internal/ConfigValidator.cpp`, replace the non-critical TLS check with a critical one:

```cpp
    // --- CRITICAL: TLS cert/key required when TLS is enabled ---
    if (cfg.tlsEnabled) {
        if (cfg.tlsCertFile.empty()) {
            return Result<std::vector<std::string>>::fail(ApiError{
                ErrorCode::InvalidConfig,
                "tlsCertFile must not be empty when TLS is enabled"
            });
        }
        if (cfg.tlsKeyFile.empty()) {
            return Result<std::vector<std::string>>::fail(ApiError{
                ErrorCode::InvalidConfig,
                "tlsKeyFile must not be empty when TLS is enabled"
            });
        }
    }
```

Remove the old non-critical TLS checks from the warnings section.

- [ ] **Step 2: Enforce TLS compilation in HttpServer**

In `src/server/HttpServer.cpp`, replace the TLS server creation block:

```cpp
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    if (cfg.tlsEnabled) {
        m_impl->httpServer = std::make_unique<httplib::SSLServer>(
            cfg.tlsCertFile.c_str(), cfg.tlsKeyFile.c_str());
        spdlog::info("[HttpServer] TLS enabled with cert={}", cfg.tlsCertFile);
    } else {
        m_impl->httpServer = std::make_unique<httplib::Server>();
    }
#else
    if (cfg.tlsEnabled) {
        throw std::runtime_error(
            "[HttpServer] TLS is enabled but this build does not include OpenSSL support. "
            "Rebuild with CPPHTTPLIB_OPENSSL_SUPPORT or disable TLS.");
    }
    m_impl->httpServer = std::make_unique<httplib::Server>();
#endif
```

- [ ] **Step 3: Build and test**

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "fix(security): TLS mandatory when enabled — refuse to start without cert/key

TLS validation moved from non-critical warning to critical config error.
Server refuses to start if TLS enabled without cert/key files, or if
OpenSSL support not compiled in."
```

---

## Task 4: Token revocation persistence — no silent fallback

**Files:**
- Modify: `src/auth/internal/TokenStore.hpp`
- Modify: `src/auth/internal/TokenStore.cpp`
- Modify: `src/auth/JwtAuth.cpp`

- [ ] **Step 1: Add factory to TokenStore**

In `src/auth/internal/TokenStore.hpp`, add factory method:
```cpp
    /// @brief Creates a TokenStore. Fails if dbPath is non-empty but DB cannot be opened.
    /// Empty dbPath = in-memory mode (test/dev only).
    static Result<std::unique_ptr<TokenStore>> create(const std::string& dbPath = {});
```

Make existing constructor private:
```cpp
private:
    explicit TokenStore() = default;
```

Add `#include "hub32api/core/Result.hpp"` at top.

- [ ] **Step 2: Implement factory in TokenStore.cpp**

Replace constructor with:
```cpp
TokenStore::TokenStore(const std::string& /*unused*/) {}

Result<std::unique_ptr<TokenStore>> TokenStore::create(const std::string& dbPath)
{
    auto store = std::unique_ptr<TokenStore>(new TokenStore());

    if (!dbPath.empty()) {
        store->initDb(dbPath);
        if (!store->m_useSqlite) {
            return Result<std::unique_ptr<TokenStore>>::fail(ApiError{
                ErrorCode::InternalError,
                "[TokenStore] Failed to open revocation database: " + dbPath
            });
        }
    }

    return Result<std::unique_ptr<TokenStore>>::ok(std::move(store));
}
```

Also in `initDb()`, check PRAGMA return codes:
```cpp
    rc = sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::warn("[TokenStore] PRAGMA journal_mode=WAL failed: {}", sqlite3_errmsg(m_db));
    }
```

- [ ] **Step 3: Update JwtAuth to use TokenStore::create()**

In `src/auth/JwtAuth.cpp`, in the `create()` factory, replace:
```cpp
    impl->store = std::make_unique<internal::TokenStore>(cfg.tokenRevocationFile);
```
with:
```cpp
    auto storeResult = internal::TokenStore::create(cfg.tokenRevocationFile);
    if (storeResult.is_err()) {
        return Result<std::unique_ptr<JwtAuth>>::fail(storeResult.error());
    }
    impl->store = storeResult.take();
```

- [ ] **Step 4: Build and test**

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "fix(security): token revocation persistence — no silent in-memory fallback

TokenStore::create() factory replaces constructor. If a dbPath is
configured but SQLite open fails, create() returns an error instead
of silently falling back to in-memory mode. Ensures revoked tokens
persist across server restarts."
```

---

## Task 5: AuditLog SQLite error handling

**Files:**
- Modify: `src/core/AuditLog.cpp`

- [ ] **Step 1: Fix constructor — check PRAGMAs, fatal on table creation failure**

In `src/core/AuditLog.cpp` constructor, replace lines 72-82:
```cpp
    // Enable WAL mode for concurrent reads
    rc = sqlite3_exec(m_impl->db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::warn("[AuditLog] PRAGMA journal_mode=WAL failed: {} (non-fatal)",
                     sqlite3_errmsg(m_impl->db));
    }
    rc = sqlite3_exec(m_impl->db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::warn("[AuditLog] PRAGMA synchronous=NORMAL failed: {} (non-fatal)",
                     sqlite3_errmsg(m_impl->db));
    }

    // Create tables — FATAL if this fails (audit log cannot function)
    char* errMsg = nullptr;
    rc = sqlite3_exec(m_impl->db, k_createTableSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        spdlog::error("[AuditLog] failed to create tables: {} — audit logging disabled",
                      errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
        sqlite3_close(m_impl->db);
        m_impl->db = nullptr;
        return;
    }
```

- [ ] **Step 2: Fix writerLoop — ROLLBACK on batch failure**

Replace the batch processing section in `writerLoop()`:
```cpp
        bool batchFailed = false;
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
                batchFailed = true;
                sqlite3_reset(stmt);
                break;
            }
            sqlite3_reset(stmt);
            batch.pop();
        }

        if (batchFailed) {
            rc = sqlite3_exec(m_impl->db, "ROLLBACK", nullptr, nullptr, nullptr);
            if (rc != SQLITE_OK) {
                spdlog::error("[AuditLog] ROLLBACK failed: {}", sqlite3_errmsg(m_impl->db));
            }
        } else {
            rc = sqlite3_exec(m_impl->db, "COMMIT", nullptr, nullptr, nullptr);
            if (rc != SQLITE_OK) {
                spdlog::error("[AuditLog] COMMIT failed: {}", sqlite3_errmsg(m_impl->db));
            }
        }
```

- [ ] **Step 3: Build and test**

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "fix(security): AuditLog checks all SQLite return codes, ROLLBACK on failure

PRAGMA results now checked and logged. Table creation failure is fatal
(disables audit log). Batch inserts ROLLBACK on first error instead of
continuing and committing partial data."
```

---

## Task 6: Input validation — safe_stoi, depth-before-recurse

**Files:**
- Modify: `src/utils/string_utils.hpp`
- Modify: `src/utils/string_utils.cpp`
- Modify: `include/hub32api/core/Constants.hpp`
- Modify: `src/api/v1/middleware/InputValidationMiddleware.cpp`
- Modify: `src/api/v1/controllers/ComputerController.cpp`
- Modify: `src/api/v1/controllers/FramebufferController.cpp`

- [ ] **Step 1: Add safe_stoi and safe_stod to string_utils**

In `src/utils/string_utils.hpp`, add:
```cpp
/// @brief Safe integer parse. Returns nullopt for empty, too-long (> 20 chars),
/// or non-parseable input. No exceptions thrown.
std::optional<int> safe_stoi(std::string_view s);

/// @brief Safe double parse. Returns nullopt for empty, too-long (> 40 chars),
/// NaN, Inf, or non-parseable input. No exceptions thrown.
std::optional<double> safe_stod(std::string_view s);
```

Add `#include <optional>` at top.

In `src/utils/string_utils.cpp`, implement:
```cpp
std::optional<int> safe_stoi(std::string_view s)
{
    if (s.empty() || s.size() > 20) return std::nullopt;
    // Skip leading whitespace
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    if (start >= s.size()) return std::nullopt;

    try {
        size_t pos = 0;
        int val = std::stoi(std::string(s.substr(start)), &pos);
        // Ensure entire string was consumed (no trailing garbage)
        if (pos + start != s.size()) return std::nullopt;
        return val;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> safe_stod(std::string_view s)
{
    if (s.empty() || s.size() > 40) return std::nullopt;
    std::string str(s);
    // Reject NaN and Infinity
    auto lower = to_lower(str);
    if (lower.find("nan") != std::string::npos ||
        lower.find("inf") != std::string::npos) return std::nullopt;

    try {
        size_t pos = 0;
        double val = std::stod(str, &pos);
        if (pos != str.size()) return std::nullopt;
        return val;
    } catch (...) {
        return std::nullopt;
    }
}
```

- [ ] **Step 2: Add kMaxQueryParamLength to Constants.hpp**

```cpp
constexpr size_t kMaxQueryParamLength = 200;  // characters — max query parameter length
```

- [ ] **Step 3: Fix depth check in InputValidationMiddleware**

In `src/api/v1/middleware/InputValidationMiddleware.cpp`, change line 127:
```cpp
    if (depth >= m_cfg.maxPathDepth) {   // >= not > (check BEFORE recursing deeper)
```

- [ ] **Step 4: Replace stoi in ComputerController**

In `src/api/v1/controllers/ComputerController.cpp`, add `#include "utils/string_utils.hpp"` and replace:
```cpp
    int limit = kDefaultPageSize;
    if (req.has_param("limit")) {
        limit = hub32api::utils::safe_stoi(req.get_param_value("limit")).value_or(kDefaultPageSize);
        limit = std::clamp(limit, 1, kMaxPageSize);
    }
```

- [ ] **Step 5: Replace stoi in FramebufferController**

In `src/api/v1/controllers/FramebufferController.cpp`, add `#include "utils/string_utils.hpp"` and replace all `try { std::stoi(...) } catch (...) {}` blocks:
```cpp
    if (!widthParam.empty()) {
        width = hub32api::utils::safe_stoi(widthParam).value_or(0);
    }
    if (!heightParam.empty()) {
        height = hub32api::utils::safe_stoi(heightParam).value_or(0);
    }
    // ...
    if (!compParam.empty()) {
        compression = std::clamp(
            hub32api::utils::safe_stoi(compParam).value_or(-1), 0, 9);
    }
    if (!qualParam.empty()) {
        quality = std::clamp(
            hub32api::utils::safe_stoi(qualParam).value_or(-1), 0, 100);
    }
```

- [ ] **Step 6: Build and test**

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "fix(security): input validation — safe_stoi, depth-before-recurse

Add safe_stoi/safe_stod to string_utils (no exceptions, length-limited).
Fix JSON depth check to use >= (check BEFORE recursion, not after).
Replace all try/catch stoi in controllers with safe_stoi.
Add kMaxQueryParamLength constant."
```

---

## Task 7: Argon2id password hashing via libargon2

**Files:**
- Create: `cmake/deps/FindArgon2.cmake`
- Modify: `cmake/FindOrFetchDeps.cmake`
- Modify: `src/core/CMakeLists.txt`
- Modify: `include/hub32api/core/Constants.hpp`
- Modify: `src/auth/UserRoleStore.cpp`

- [ ] **Step 1: Create FindArgon2.cmake**

Create `cmake/deps/FindArgon2.cmake`:
```cmake
# FindArgon2.cmake — FetchContent for phc-winner-argon2 (reference implementation)
include(FetchContent)

FetchContent_Declare(argon2
    GIT_REPOSITORY https://github.com/P-H-C/phc-winner-argon2.git
    GIT_TAG        20190702   # v20190702 — latest stable release
)
FetchContent_MakeAvailable(argon2)

if(NOT TARGET Argon2::argon2)
    add_library(argon2_lib STATIC
        ${argon2_SOURCE_DIR}/src/argon2.c
        ${argon2_SOURCE_DIR}/src/core.c
        ${argon2_SOURCE_DIR}/src/encoding.c
        ${argon2_SOURCE_DIR}/src/thread.c
        ${argon2_SOURCE_DIR}/src/blake2/blake2b.c
        ${argon2_SOURCE_DIR}/src/ref.c
    )
    target_include_directories(argon2_lib PUBLIC
        ${argon2_SOURCE_DIR}/include
    )
    target_compile_definitions(argon2_lib PRIVATE ARGON2_NO_THREADS)
    add_library(Argon2::argon2 ALIAS argon2_lib)
endif()
```

- [ ] **Step 2: Add to FindOrFetchDeps.cmake**

In `cmake/FindOrFetchDeps.cmake`, add:
```cmake
include(deps/FindArgon2)
mark_as_system(Argon2::argon2)
```

- [ ] **Step 3: Link argon2 to hub32api-core**

In `src/core/CMakeLists.txt`, add to `target_link_libraries(hub32api-core PRIVATE ...)`:
```cmake
        Argon2::argon2
```

- [ ] **Step 4: Add Argon2 constants**

In `include/hub32api/core/Constants.hpp`, add:
```cpp
// -----------------------------------------------------------------------
// Argon2id password hashing (OWASP 2024 recommended)
// -----------------------------------------------------------------------
constexpr int kArgon2TimeCost    = 3;      // iterations
constexpr int kArgon2MemoryCost  = 65536;  // KiB — 64 MB
constexpr int kArgon2Parallelism = 1;      // threads — single-threaded for ARGON2_NO_THREADS
constexpr int kArgon2HashBytes   = 32;     // bytes — output hash length
constexpr int kArgon2SaltBytes   = 16;     // bytes — salt length
```

- [ ] **Step 5: Update UserRoleStore — Argon2id for new hashes, PBKDF2 for old**

In `src/auth/UserRoleStore.cpp`:

Add `#include <argon2.h>` at top.

Replace `hashPassword()`:
```cpp
Result<std::string> UserRoleStore::hashPassword(const std::string& password)
{
    // Generate cryptographically secure salt
    unsigned char salt[kArgon2SaltBytes];
    if (RAND_bytes(salt, kArgon2SaltBytes) != 1) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::CryptoFailure, "RAND_bytes failed for password salt"
        });
    }

    // Argon2id hash
    char encoded[256];
    int rc = argon2id_hash_encoded(
        kArgon2TimeCost,
        kArgon2MemoryCost,
        kArgon2Parallelism,
        password.c_str(), password.size(),
        salt, kArgon2SaltBytes,
        kArgon2HashBytes,
        encoded, sizeof(encoded));

    if (rc != ARGON2_OK) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::CryptoFailure,
            std::string("argon2id_hash_encoded failed: ") + argon2_error_message(rc)
        });
    }

    return Result<std::string>::ok(std::string(encoded));
}
```

Update `verifyPassword()` to detect format and dispatch:
```cpp
bool UserRoleStore::verifyPassword(const std::string& password, const std::string& storedHash)
{
    // Argon2id format: $argon2id$...
    if (storedHash.rfind("$argon2id$", 0) == 0) {
        return argon2id_verify(storedHash.c_str(), password.c_str(), password.size()) == ARGON2_OK;
    }

    // Legacy PBKDF2-SHA256 format: $pbkdf2-sha256$...
    if (storedHash.rfind("$pbkdf2-sha256$", 0) == 0) {
        // ... keep existing PBKDF2 verification code ...
        // (existing split-by-$ logic, PKCS5_PBKDF2_HMAC, CRYPTO_memcmp)
    }

    return false;  // Unknown format
}
```

Keep the full existing PBKDF2 verification code inside the `$pbkdf2-sha256$` branch.

- [ ] **Step 6: Configure and build**

Need to reconfigure CMake to fetch argon2:
```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
rm -rf build/debug
PATH="/c/msys64/mingw64/bin:$PATH" cmake --preset debug
PATH="/c/msys64/mingw64/bin:$PATH" cmake --build build/debug 2>&1 | tail -30
```

- [ ] **Step 7: Run tests**

Existing password tests should still pass (PBKDF2 backwards compat).

- [ ] **Step 8: Commit**

```bash
git add -A && git commit -m "fix(security): Argon2id password hashing via libargon2

New passwords use Argon2id (OWASP 2024 recommended): t=3, m=64MB, p=1.
Existing PBKDF2-SHA256 hashes still verified for backwards compatibility.
libargon2 fetched via FetchContent from phc-winner-argon2 repository."
```

---

## Task 8: Rate limiter per-endpoint + document clocks

**Files:**
- Modify: `include/hub32api/core/Constants.hpp`
- Modify: `src/api/v1/middleware/RateLimitMiddleware.hpp`
- Modify: `src/api/v1/middleware/RateLimitMiddleware.cpp`

- [ ] **Step 1: Add auth rate limit constant**

In `include/hub32api/core/Constants.hpp`:
```cpp
constexpr int kAuthRateRequestsPerMinute = 10;  // requests — auth endpoint brute force protection
```

- [ ] **Step 2: Change bucket key to include path**

In `src/api/v1/middleware/RateLimitMiddleware.cpp`:

Add clock documentation comment at top of `process()`:
```cpp
    // DESIGN NOTE — Clock Selection:
    // steady_clock for rate calculations (immune to NTP adjustments, leap seconds).
    // system_clock ONLY for X-RateLimit-Reset header (Unix timestamp for HTTP clients).
    // These are intentionally different clocks serving different purposes:
    //   - steady_clock: monotonic, reliable elapsed-time measurement for token refill
    //   - system_clock: wall-clock time that clients can interpret as Unix timestamp
```

Change bucket key from `req.remote_addr` to include path:
```cpp
    const std::string key = req.remote_addr + ":" + req.path;
```

- [ ] **Step 3: Add per-endpoint config to RateLimitMiddleware**

In `src/api/v1/middleware/RateLimitMiddleware.hpp`, add endpoint override:
```cpp
struct RateLimitConfig {
    int requestsPerMinute = hub32api::kDefaultRequestsPerMinute;  // requests
    int burstSize         = hub32api::kDefaultBurstSize;          // requests
    /// @brief Per-path overrides. Key is path prefix (e.g., "/api/v1/auth").
    std::unordered_map<std::string, int> endpointLimits;
};
```

Add `#include <unordered_map>` at top.

In `src/api/v1/middleware/RateLimitMiddleware.cpp`, resolve the effective rate for the request:
```cpp
    // Resolve per-endpoint rate limit
    int effectiveRpm = m_cfg.requestsPerMinute;
    for (const auto& [prefix, rpm] : m_cfg.endpointLimits) {
        if (req.path.rfind(prefix, 0) == 0) {
            effectiveRpm = rpm;
            break;
        }
    }
    const double ratePerSecond = static_cast<double>(effectiveRpm) / 60.0;
```

- [ ] **Step 4: Wire auth endpoint rate limit in Router**

In `src/server/Router.cpp` (or wherever RateLimitMiddleware is constructed), add:
```cpp
    rateCfg.endpointLimits["/api/v1/auth"] = hub32api::kAuthRateRequestsPerMinute;
```

Search for where `RateLimitConfig` is constructed and add the endpoint override there.

- [ ] **Step 5: Build and test**

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "fix(security): rate limiter per-endpoint with auth brute-force protection

Bucket key now includes request path (per-IP + per-endpoint).
Auth endpoint limited to 10 req/min (vs 120 default) for brute-force
protection. Clock selection documented: steady_clock for rate math,
system_clock for HTTP header timestamp."
```

---

## Task 9: Final verification

- [ ] **Step 1: Full clean rebuild**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
rm -rf build/debug
PATH="/c/msys64/mingw64/bin:$PATH" cmake --preset debug -DBUILD_TESTS=ON
PATH="/c/msys64/mingw64/bin:$PATH" cmake --build build/debug 2>&1 | tail -30
```

- [ ] **Step 2: Run all tests**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api/build/debug
PATH="/c/msys64/mingw64/bin:$PATH" ctest --output-on-failure
```

- [ ] **Step 3: Verify security items**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
echo "=== HS256 references ==="
grep -rn 'HS256' src/ --include='*.cpp' --include='*.hpp' | grep -v '//' | grep -v 'deprecated' || echo "CLEAN"

echo "=== mt19937 ==="
grep -rn 'mt19937\|random_device\|std::rand\b\|srand' src/ --include='*.cpp' | grep -v '//' || echo "CLEAN"

echo "=== Hardcoded admin ==="
grep -rn 'role.*=.*"admin"' src/ --include='*.cpp' || echo "CLEAN"

echo "=== try stoi without safe ==="
grep -rn 'std::stoi\|std::stod\|atoi\b' src/ --include='*.cpp' | grep -v safe_sto | grep -v test_ | grep -v '//' || echo "CLEAN"

echo "=== Remaining throws ==="
grep -rn 'throw std::runtime_error' src/ --include='*.cpp' | grep -v HttpServer || echo "CLEAN"
```

- [ ] **Step 4: Show git log**

```bash
git log --oneline -10
```

- [ ] **Step 5: Report remaining concerns**
