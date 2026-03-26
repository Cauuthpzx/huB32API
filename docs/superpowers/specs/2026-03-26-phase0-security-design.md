# Phase 0: Critical Security Fixes — Design Spec

**Date:** 2026-03-26
**Scope:** 8 security items (3 of 11 already fixed)
**Namespace:** `hub32api::` (unchanged)

---

## Already Fixed (verified by audit)

| Item | Status | Evidence |
|------|--------|---------|
| #2 mt19937 | DONE | Zero instances in code. All random via RAND_bytes(). |
| #3 Hardcoded admin | DONE | Role from UserRoleStore after PBKDF2 verification. |
| #7 Clock mixing in TokenStore | DONE | Consistent system_clock throughout. |

---

## Item #1: Remove HS256, RS256-only with RSA 4096-bit

### Current state
- `JwtAlgorithm` enum has `RS256` and `HS256`
- `JwtAuth::create()` accepts both algorithms
- `JwtValidator` accepts both algorithms
- `ServerConfig.jwtAlgorithm` defaults to `"RS256"` but `"HS256"` is valid
- `ConfigValidator` validates that algorithm is RS256 or HS256

### Changes needed

**`include/hub32api/core/Constants.hpp`:**
- Remove `JwtAlgorithm::HS256` from enum
- Remove `jwt_algorithm_from_string()` (only one algorithm now)
- Keep `to_string(JwtAlgorithm)` returning `"RS256"` always
- Add `constexpr int kRsaMinKeyBits = 4096; // bits — minimum RSA key size`

**`src/core/Constants.cpp`:**
- Simplify `to_string(JwtAlgorithm)` — only RS256 case
- Remove `jwt_algorithm_from_string()`

**`src/auth/JwtAuth.cpp` (`create()` factory):**
- Remove HS256 code path entirely
- If `cfg.jwtAlgorithm != "RS256"`: fail with InvalidConfig
- Always require private + public key files
- Remove `m_impl->secret` field usage for signing
- Keep the secret field in Impl for backwards compat but don't use it for signing

**`src/auth/internal/JwtValidator.cpp`:**
- Remove HS256 verification path
- Only `jwt::algorithm::rs256` allowed
- Reject any token not signed with RS256

**`src/config/internal/ConfigValidator.cpp`:**
- Remove `"HS256"` from valid algorithm check
- Only accept `"RS256"`

**`include/hub32api/config/ServerConfig.hpp`:**
- Remove `jwtSecret` field (or deprecate — keep empty, unused)
- Actually: keep field for backwards compat with config files, but it's ignored. Log warning if set.

**`src/config/ServerConfig.cpp`:**
- Remove `generateRandomSecret()` function entirely
- Remove all code that generates/validates jwtSecret
- If `jwtSecret` is present in config, log deprecation warning

**Tests:**
- Update `test_jwt_auth.cpp` — remove HS256 test cases, add RS256-only enforcement test
- Add test: attempting to create JwtAuth with algorithm="HS256" fails
- Add test: token signed with HS256 is rejected by validator

---

## Item #10: Config fail-on-error

### Current state
- `ConfigValidator::validate()` returns `Result<vector<string>>` — critical errors fail, non-critical return warning list
- `ServerConfig::from_file()` and `from_registry()`: on critical validation failure, call `defaults()` instead of propagating error
- `main.cpp`: no validation of loaded config, server starts regardless

### Changes needed

**`src/config/ServerConfig.cpp`:**
- `from_file()`: on critical validation error, DO NOT fall back to defaults. Return the error.
- Change return type from `ServerConfig` to `Result<ServerConfig>`
- `from_registry()`: same change
- `defaults()`: keep as-is (no validation needed for defaults)

**`include/hub32api/config/ServerConfig.hpp`:**
- Change signatures:
  ```cpp
  static Result<ServerConfig> from_file(const std::string& path);
  static Result<ServerConfig> from_registry();
  static ServerConfig defaults();  // unchanged
  ```

**`src/service/main.cpp`:**
- Handle `Result<ServerConfig>` — if error, log CRITICAL and return 1
- Server MUST NOT start if config is invalid

**`src/server/HttpServer.cpp`:**
- Already throws on JwtAuth failure — that's correct
- Add validation: if TLS enabled but cert/key empty, throw with clear message
- Add validation: if `tokenRevocationFile` is non-empty, verify path is writable

**All callers of `from_file()`/`from_registry()`:**
- Tests, examples, etc. — update to handle Result<ServerConfig>

---

## Item #5: TLS mandatory when enabled

### Current state
- `HttpServer` creates `SSLServer` when `CPPHTTPLIB_OPENSSL_SUPPORT` defined and `tlsEnabled=true`
- If OpenSSL not compiled in but tlsEnabled=true: just warns
- No cert file existence check before creating SSLServer

### Changes needed

**`src/server/HttpServer.cpp`:**
- If `cfg.tlsEnabled && !CPPHTTPLIB_OPENSSL_SUPPORT`: throw, don't warn
- If `cfg.tlsEnabled` and cert or key file empty: throw
- If `cfg.tlsEnabled` and cert or key file doesn't exist on disk: throw
- Add descriptive error messages with instructions for generating certs

**`src/config/internal/ConfigValidator.cpp`:**
- Move TLS validation from non-critical to critical:
  ```cpp
  if (cfg.tlsEnabled) {
      if (cfg.tlsCertFile.empty() || cfg.tlsKeyFile.empty()) {
          return Result::fail(ApiError{ErrorCode::InvalidConfig, "TLS requires cert and key files"});
      }
  }
  ```

---

## Item #11: Token revocation persistence (no silent fallback)

### Current state
- TokenStore uses SQLite when `dbPath` is provided
- On SQLite open failure: falls back to in-memory silently
- `purgeExpired()` exists but is only called periodically in `JwtAuth::authenticate()` (every 100 calls)
- No startup validation of DB path

### Changes needed

**`src/auth/internal/TokenStore.hpp`:**
- Change constructor to return Result via factory pattern:
  ```cpp
  static Result<std::unique_ptr<TokenStore>> create(const std::string& dbPath);
  ```
- Remove in-memory fallback when dbPath is configured
- Keep in-memory mode ONLY when dbPath is explicitly empty (test/dev mode)

**`src/auth/internal/TokenStore.cpp`:**
- `create()`: if dbPath non-empty but SQLite open fails, return `Result::fail()`
- `initDb()`: check PRAGMA return codes, log errors
- Add periodic auto-purge in `isRevoked()` (every N calls, like JwtAuth already does)

**`src/auth/JwtAuth.cpp`:**
- Update to use `TokenStore::create()` factory
- Propagate error if TokenStore creation fails

---

## Item #8: AuditLog SQLite error handling

### Current state
- PRAGMA results unchecked (lines 73-74)
- Table creation failure logged but not fatal (lines 77-82)
- Batch insert: BEGIN failure drops batch, no ROLLBACK, COMMIT may run after failed inserts

### Changes needed

**`src/core/AuditLog.cpp`:**

1. Constructor: check PRAGMA results
   ```cpp
   rc = sqlite3_exec(m_impl->db, "PRAGMA journal_mode=WAL;", ...);
   if (rc != SQLITE_OK) { spdlog::error(...); /* continue — WAL is a preference, not required */ }
   ```

2. Table creation failure → fatal: close DB, set `m_impl->db = nullptr`
   ```cpp
   if (rc != SQLITE_OK) {
       spdlog::error("[AuditLog] failed to create tables — audit logging disabled");
       sqlite3_close(m_impl->db);
       m_impl->db = nullptr;
       return;
   }
   ```

3. writerLoop: on INSERT failure, ROLLBACK instead of continuing
   ```cpp
   bool batchFailed = false;
   while (!batch.empty()) {
       // ... bind and step ...
       if (rc != SQLITE_DONE) {
           spdlog::error("[AuditLog] insert failed: {}", sqlite3_errmsg(...));
           batchFailed = true;
           break;
       }
   }
   if (batchFailed) {
       sqlite3_exec(m_impl->db, "ROLLBACK", ...);
   } else {
       sqlite3_exec(m_impl->db, "COMMIT", ...);
   }
   ```

---

## Item #9: Input validation (safe_stoi, depth-before-recurse)

### Current state
- `validateJsonValue()`: depth check at line 127 happens AFTER recursion entry
- Multiple controllers use `try { std::stoi(...) } catch (...) {}` without length limits
- Query parameters not length-validated before parsing

### Changes needed

**`src/utils/string_utils.hpp` + `.cpp`:**
- Add `safe_stoi()`: validates length (<= 20 chars), uses `std::from_chars`, returns `std::optional<int>`
- Add `safe_stod()`: validates length (<= 40 chars), returns `std::optional<double>`

```cpp
/// @brief Safe integer parse. Returns nullopt for empty, too long (> 20 chars),
/// or non-parseable input. No exceptions thrown.
std::optional<int> safe_stoi(std::string_view s);

/// @brief Safe double parse. Returns nullopt for empty, too long (> 40 chars),
/// NaN, Inf, or non-parseable input. No exceptions thrown.
std::optional<double> safe_stod(std::string_view s);
```

**`src/api/v1/middleware/InputValidationMiddleware.cpp`:**
- Move depth check BEFORE recursion:
  ```cpp
  bool InputValidationMiddleware::validateJsonValue(const nlohmann::json& j,
                                                    int depth,
                                                    std::string& violation) const
  {
      if (depth >= m_cfg.maxPathDepth) {  // CHECK FIRST, before processing
          violation = "JSON nesting depth exceeds maximum";
          return false;
      }
      // ... then process j's contents with depth+1 ...
  }
  ```

**All controllers with `std::stoi`:**
- Replace `try { std::stoi(...) } catch (...) {}` with `utils::safe_stoi(...).value_or(default)`
- Files: ComputerController.cpp, FramebufferController.cpp
- Also: I18n.cpp `std::stod` → `utils::safe_stod`
- Also: UserRoleStore.cpp `std::stoi` → `utils::safe_stoi`

**Query parameter length validation:**
- Add `constexpr size_t kMaxQueryParamLength = 200; // characters` to Constants.hpp
- In controllers, before parsing: `if (param.size() > kMaxQueryParamLength) { sendError(...); return; }`

---

## Item #4: Argon2id via libargon2

### Current state
- PBKDF2-SHA256 with 310k iterations (OWASP compliant but not state-of-art)
- Constants: `kPbkdf2Iterations`, `kPbkdf2SaltBytes`, `kPbkdf2HashBytes`
- Format: `$pbkdf2-sha256$iterations$salt$hash`

### Changes needed

**`cmake/deps/FindArgon2.cmake`:**
- FetchContent from https://github.com/P-H-C/phc-winner-argon2
- Build as static library
- Create `Argon2::argon2` imported target

**`cmake/FindOrFetchDeps.cmake`:**
- Add `include(deps/FindArgon2)`
- Add `mark_as_system(Argon2::argon2)`

**`src/core/CMakeLists.txt`:**
- Add `Argon2::argon2` to `target_link_libraries(hub32api-core PRIVATE ...)`

**`include/hub32api/core/Constants.hpp`:**
- Add Argon2 constants:
  ```cpp
  constexpr int kArgon2TimeCost    = 3;     // iterations — OWASP recommended
  constexpr int kArgon2MemoryCost  = 65536; // KiB — 64 MB
  constexpr int kArgon2Parallelism = 4;     // threads
  constexpr int kArgon2HashBytes   = 32;    // bytes — output hash length
  constexpr int kArgon2SaltBytes   = 16;    // bytes — salt length
  ```

**`src/auth/UserRoleStore.cpp`:**
- `hashPassword()`: use Argon2id instead of PBKDF2
  - Format: `$argon2id$v=19$m=65536,t=3,p=4$salt_base64$hash_base64`
  - Use `argon2id_hash_encoded()` from libargon2
- `verifyPassword()`: detect format by prefix
  - `$argon2id$...` → use `argon2id_verify()`
  - `$pbkdf2-sha256$...` → use existing PBKDF2 verification (backwards compat)
  - This allows transparent migration: existing hashes verify, new hashes use Argon2id

**Tests:**
- Test Argon2id hash + verify roundtrip
- Test PBKDF2 backwards compatibility (verify old hash format)
- Test that new hashes are Argon2id format

---

## Item #6: Rate limiter per-endpoint + document clock choice

### Current state
- Per-IP token bucket with `std::mutex` (thread-safe)
- `steady_clock` for refill (correct), `system_clock` for HTTP header (correct)
- No per-endpoint differentiation

### Changes needed

**`src/api/v1/middleware/RateLimitMiddleware.hpp`:**
- Change bucket key from `remote_addr` to `remote_addr + ":" + path`
- Add per-endpoint config support:
  ```cpp
  struct RateLimitConfig {
      int requestsPerMinute = kDefaultRequestsPerMinute;
      int burstSize         = kDefaultBurstSize;
  };
  ```
  Keep existing config struct but add endpoint override map in Router.

**`src/api/v1/middleware/RateLimitMiddleware.cpp`:**
- Add clear comments documenting the clock choice:
  ```cpp
  // DESIGN: steady_clock for rate calculations (immune to system clock adjustment).
  // system_clock ONLY for X-RateLimit-Reset header (Unix timestamp for HTTP clients).
  // These are NOT mixed — steady_clock drives refill logic, system_clock drives the header value.
  ```
- Change bucket key to include request path:
  ```cpp
  const std::string key = req.remote_addr + ":" + req.path;
  ```

**`src/server/Router.cpp` (or Router setup):**
- Apply stricter rate limits to auth endpoints:
  - `/api/v1/auth` (login): 10 requests/minute per IP (brute force protection)
  - Default endpoints: 120 requests/minute per IP (existing)
- Add `constexpr int kAuthRateRequestsPerMinute = 10; // requests — auth brute force protection` to Constants.hpp

---

## Execution Order

1. **#1** Remove HS256 (highest risk, unblocks nothing)
2. **#10** Config fail-on-error (unblocks #5, #11)
3. **#5** TLS mandatory (depends on #10 config pattern)
4. **#11** Token revocation persistence (depends on #10 config pattern)
5. **#8** AuditLog SQLite error handling
6. **#9** Input validation (safe_stoi, depth-before-recurse)
7. **#4** Argon2id (independent, needs FetchContent setup)
8. **#6** Rate limiter per-endpoint (independent)

Each item: build + test + commit `fix(security): description`.

---

## Files Affected Summary

| File | Items |
|------|-------|
| `include/hub32api/core/Constants.hpp` | #1, #4, #6, #9 |
| `src/core/Constants.cpp` | #1 |
| `src/auth/JwtAuth.cpp` | #1, #11 |
| `src/auth/JwtAuth.hpp` | #1 |
| `src/auth/internal/JwtValidator.cpp` | #1 |
| `src/auth/UserRoleStore.cpp` | #4, #9 |
| `src/auth/internal/TokenStore.hpp` | #11 |
| `src/auth/internal/TokenStore.cpp` | #11 |
| `src/config/internal/ConfigValidator.cpp` | #1, #5, #10 |
| `src/config/ServerConfig.hpp` | #10 |
| `src/config/ServerConfig.cpp` | #1, #10 |
| `src/server/HttpServer.cpp` | #5, #10 |
| `src/service/main.cpp` | #10 |
| `src/core/AuditLog.cpp` | #8 |
| `src/api/v1/middleware/InputValidationMiddleware.cpp` | #9 |
| `src/api/v1/middleware/RateLimitMiddleware.hpp` | #6 |
| `src/api/v1/middleware/RateLimitMiddleware.cpp` | #6 |
| `src/api/v1/controllers/ComputerController.cpp` | #9 |
| `src/api/v1/controllers/FramebufferController.cpp` | #9 |
| `src/utils/string_utils.hpp` | #9 |
| `src/utils/string_utils.cpp` | #9 |
| `cmake/deps/FindArgon2.cmake` | #4 |
| `cmake/FindOrFetchDeps.cmake` | #4 |
| `src/core/CMakeLists.txt` | #4 |
