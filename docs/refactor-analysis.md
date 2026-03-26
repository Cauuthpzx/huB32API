# Hub32API Refactor Analysis

> Auto-generated: 2026-03-26
> Scope: `hub32api/` project only (excludes veyon/, third-party, build artifacts)

---

## 1. #define Constants -> constexpr

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/auth/JwtAuth.cpp` | 19 | `#define JWT_DISABLE_PICOJSON` | Third-party config flag - acceptable exception |
| `src/auth/internal/JwtValidator.cpp` | 16 | `#define JWT_DISABLE_PICOJSON` | Third-party config flag - acceptable exception |
| `include/hub32api/export.h` | 18 | `#define HUB32API_DEPRECATED __declspec(deprecated)` | CMake-generated export macro - acceptable exception |
| `include/hub32api/export.h` | 19 | `#define HUB32API_DEPRECATED_EXPORT` | CMake-generated export macro - acceptable exception |
| `include/hub32api/plugins/PluginInterface.hpp` | 31 | `#define HUB32API_PLUGIN_METADATA(UID, NAME, DESC, VER)` | Macro generates functions (needs `__FILE__`, `__LINE__`) - acceptable per CLAUDE.md |
| `agent/include/hub32agent/features/ScreenLock.hpp` | 20, 23 | `#define WIN32_LEAN_AND_MEAN`, `#define NOMINMAX` | Required before `<windows.h>` - acceptable exception |
| `agent/src/features/InputLock.cpp` | 12, 15 | `#define WIN32_LEAN_AND_MEAN`, `#define NOMINMAX` | Required before `<windows.h>` - acceptable exception |
| `agent/src/features/MessageDisplay.cpp` | 12, 15 | `#define WIN32_LEAN_AND_MEAN`, `#define NOMINMAX` | Required before `<windows.h>` - acceptable exception |
| `agent/src/features/PowerControl.cpp` | 12, 15 | `#define WIN32_LEAN_AND_MEAN`, `#define NOMINMAX` | Required before `<windows.h>` - acceptable exception |
| `agent/src/features/ScreenCapture.cpp` | 21 | `#define STB_IMAGE_WRITE_IMPLEMENTATION` | Third-party header-only lib activation - acceptable exception |
| `agent/src/features/ScreenCapture.cpp` | 28, 31 | `#define WIN32_LEAN_AND_MEAN`, `#define NOMINMAX` | Required before `<windows.h>` - acceptable exception |
| `agent/src/features/ScreenLock.cpp` | 11, 14 | `#define WIN32_LEAN_AND_MEAN`, `#define NOMINMAX` | Required before `<windows.h>` - acceptable exception |

**Verdict:** All `#define` usages are legitimate exceptions (third-party config, Windows API, CMake export). No violations found.

---

## 2. Magic Numbers/Strings -> constexpr / enum class

### 2.1 Time/Duration Magic Numbers

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/server/Router.cpp` | 148 | `int port = 11100` (VNC port hardcoded) | `constexpr int kDefaultVncPort = 11100; // VNC default port` in `hub32::constants` |
| `src/server/Router.cpp` | 148, 194 | `int timeoutMs = 1500` (TCP timeout) | `constexpr int kTcpTimeoutMs = 1500; // milliseconds` |
| `src/server/Router.cpp` | 204 | `tcpPing(host, vncPort, 500)` (ping timeout) | `constexpr int kPingTimeoutMs = 500; // milliseconds` |
| `src/api/v1/controllers/AuthController.cpp` | 131 | `3600` (JWT token expiry) | `constexpr int kTokenExpirySec = 3600; // seconds` |
| `src/api/v1/dto/AuthDto.hpp` | 23 | `int expiresIn = 3600` | Use `kTokenExpirySec` from `hub32::constants` |
| `src/api/v1/middleware/CorsMiddleware.hpp` | 15 | `int maxAgeSec = 3600` | `constexpr int kCorsMaxAgeSec = 3600; // seconds` |
| `src/server/Router.cpp` | 748, 951, 1071, 1187, 1395 | `"max-age=3600"` in Cache-Control headers | `constexpr int kCacheMaxAgeSec = 3600; // seconds` |
| `src/agent/HeartbeatMonitor.hpp` | 37 | `m_timeout{90000}` (heartbeat timeout) | `constexpr int kHeartbeatTimeoutMs = 90000; // milliseconds (90s)` |
| `src/service/main.cpp` | 64 | `sleep_for(milliseconds(200))` | `constexpr int kServicePollIntervalMs = 200; // milliseconds` |
| `src/service/WinServiceAdapter.cpp` | 101 | `reportStatus(..., 3000)` (start wait hint) | `constexpr DWORD kServiceStartWaitMs = 3000; // milliseconds` |
| `src/service/WinServiceAdapter.cpp` | 133 | `reportStatus(..., 5000)` (stop wait hint) | `constexpr DWORD kServiceStopWaitMs = 5000; // milliseconds` |
| `src/api/v1/dto/AgentDto.hpp` | 39 | `int commandPollIntervalMs = 5000` | `constexpr int kCommandPollIntervalMs = 5000; // milliseconds` |

### 2.2 Size/Buffer Magic Numbers

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/config/ServerConfig.cpp` | 50, 62 | `unsigned char buf[32]` and loop `i < 32` | `constexpr size_t kSecretKeyBytes = 32; // bytes` |
| `src/core/CryptoUtils.cpp` | 12, 20 | `uint8_t bytes[16]` and loop `i < 16` | `constexpr size_t kUuidBytes = 16; // bytes` |
| `src/api/v1/controllers/AgentController.cpp` | 43 | `char buf[32]` | `constexpr size_t kTimestampBufSize = 32; // bytes` |
| `src/server/Router.cpp` | 224 | `sizeof(ICMP_ECHO_REPLY) + 32` | `constexpr size_t kIcmpReplyPadding = 32; // bytes` |
| `src/api/v1/middleware/InputValidationMiddleware.hpp` | 12-14 | `maxFieldLength = 1000`, `maxArraySize = 500`, `maxPathDepth = 10` | Move to `hub32::constants` with unit comments |
| `src/api/v1/middleware/RateLimitMiddleware.hpp` | 13-14 | `requestsPerMinute = 120`, `burstSize = 20` | Move defaults to `hub32::constants` |
| `src/server/Router.cpp` | 695 | `RateLimitConfig{ 120, 20 }` | Use named constants from `hub32::constants` |
| `src/api/v1/middleware/RateLimitMiddleware.cpp` | 65 | `m_callCount % 1000 == 0` (cleanup interval) | `constexpr size_t kRateLimitCleanupInterval = 1000; // requests` |
| `src/api/v1/controllers/ComputerController.cpp` | 76 | `int limit = 50` (default page size) | `constexpr int kDefaultPageSize = 50; // items` |
| `src/server/Router.cpp` | 445 | `{"default",50},{"maximum",200}` | `constexpr int kMaxPageSize = 200; // items` |

### 2.3 Magic Strings - Role/Auth (CRITICAL)

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/server/Router.cpp` | 891 | `ctx.auth.token->role != "admin"` | `enum class UserRole { Admin, Teacher, Student }` with `to_string()` |
| `src/api/v1/controllers/TeacherController.cpp` | 40 | `result.value().token->role != "admin"` | Use `UserRole::Admin` |
| `src/api/v1/controllers/SchoolController.cpp` | 41 | `result.value().token->role != "admin"` | Use `UserRole::Admin` |
| `src/server/Router.cpp` | 1413, 1473 | `authHeader.substr(0, 7) != "Bearer "` | `constexpr std::string_view kBearerPrefix = "Bearer "; constexpr size_t kBearerPrefixLen = 7;` |
| `src/api/v1/controllers/AuthController.cpp` | 160 | `authHeader.substr(7)` | Use `kBearerPrefixLen` |
| `src/api/v1/controllers/TeacherController.cpp` | 37 | `authHeader.substr(7)` | Use `kBearerPrefixLen` |
| `src/api/v1/controllers/SchoolController.cpp` | 38 | `authHeader.substr(7)` | Use `kBearerPrefixLen` |
| `src/api/v1/middleware/AuthMiddleware.cpp` | 113 | `authHeader.rfind("Bearer ", 0) != 0` | Use `kBearerPrefix` |
| `src/api/v1/controllers/AuthController.cpp` | 154 | `authHeader.rfind("Bearer ", 0) != 0` | Use `kBearerPrefix` |

### 2.4 Magic Strings - JWT Algorithm

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/auth/JwtAuth.cpp` | 133 | `cfg.jwtAlgorithm == "RS256"` | `enum class JwtAlgorithm { RS256, HS256 }` |
| `src/auth/JwtAuth.cpp` | 161 | `cfg.jwtAlgorithm == "HS256"` | Use `JwtAlgorithm::HS256` |
| `src/auth/JwtAuth.cpp` | 224 | JWT algorithm comparison | Use `JwtAlgorithm` enum |
| `src/config/internal/ConfigValidator.cpp` | 54 | `cfg.jwtAlgorithm != "RS256" && ... != "HS256"` | Use `JwtAlgorithm` enum |
| `src/auth/internal/JwtValidator.cpp` | 135 | `tokenAlg == "none" \|\| tokenAlg == "None" \|\| tokenAlg == "NONE"` | `JwtAlgorithm::None` with case-insensitive compare |
| `src/auth/internal/JwtValidator.cpp` | 161 | `m_algorithm == "RS256"` | Use `JwtAlgorithm::RS256` |

### 2.5 Magic Strings - Agent States & Command Status

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/core/ApiContext.cpp` | 56 | `if (s == "online") return AgentState::Online` | Already has enum! Move string mapping to `to_string()`/`from_string()` with constexpr lookup |
| `src/core/ApiContext.cpp` | 57 | `if (s == "busy") return AgentState::Busy` | Use constexpr string_view array for mapping |
| `src/core/ApiContext.cpp` | 58 | `if (s == "error") return AgentState::Error` | Use constexpr string_view array for mapping |
| `src/core/ApiContext.cpp` | 78 | `if (s == "running") return CommandStatus::Running` | Same: constexpr lookup table |
| `src/core/ApiContext.cpp` | 79 | `if (s == "success") return CommandStatus::Success` | Same |
| `src/core/ApiContext.cpp` | 80 | `if (s == "failed") return CommandStatus::Failed` | Same |
| `src/core/ApiContext.cpp` | 81 | `if (s == "timeout") return CommandStatus::Timeout` | Same |

### 2.6 Magic Strings - Feature UIDs & Operations

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/plugins/feature/FeaturePlugin.cpp` | 51 | `featureUid == "feat-lock-screen"` | `enum class FeatureUid` with string mapping |
| `src/plugins/feature/FeaturePlugin.cpp` | 52 | `featureUid == "feat-screen-broadcast"` | Same |
| `src/plugins/feature/FeaturePlugin.cpp` | 53 | `featureUid == "feat-input-lock"` | Same |
| `src/plugins/feature/FeaturePlugin.cpp` | 54 | `featureUid == "feat-message"` | Same |
| `src/plugins/feature/FeaturePlugin.cpp` | 55 | `featureUid == "feat-power-control"` | Same |
| `agent/src/features/ScreenLock.cpp` | 90, 131 | `operation == "start"`, `operation == "stop"` | `enum class FeatureOperation { Start, Stop }` |
| `agent/src/features/ScreenCapture.cpp` | 118, 122 | `operation == "stop"`, `operation != "start"` | Use `FeatureOperation` enum |
| `agent/src/features/PowerControl.cpp` | 73, 77 | `operation != "start"`, `operation == "stop"` | Use `FeatureOperation` enum |
| `agent/src/features/PowerControl.cpp` | 93-97 | `action == "shutdown"`, `"reboot"`, `"logoff"` | `enum class PowerAction { Shutdown, Reboot, Logoff }` |
| `agent/src/features/MessageDisplay.cpp` | 85, 136 | `operation == "start"`, `operation == "stop"` | Use `FeatureOperation` enum |
| `agent/src/features/InputLock.cpp` | 86, 105 | `operation == "start"`, `operation == "stop"` | Use `FeatureOperation` enum |

### 2.7 Magic Strings - Auth Methods (CRITICAL)

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/api/v1/controllers/AuthController.cpp` | 27 | `method == "0c69b301-81b4-42d6-8fae-128cdd113314"` -> `"hub32-key"` | `constexpr std::string_view kAuthMethodHub32KeyUid = "0c69b301-..."` |
| `src/api/v1/controllers/AuthController.cpp` | 28 | `method == "63611f7c-b457-42c7-832e-67d0f9281085"` -> `"logon"` | `constexpr std::string_view kAuthMethodLogonUid = "63611f7c-..."` |
| `src/api/v1/controllers/AuthController.cpp` | 90 | `req_dto.method == "logon"` | `enum class AuthMethod { Logon, Hub32Key }` |
| `src/api/v1/controllers/AuthController.cpp` | 100 | `req_dto.method == "hub32-key"` | Use `AuthMethod::Hub32Key` |

### 2.8 Magic Strings - HTTP Methods

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/server/Router.cpp` | 771-774, 795-797, 974-976, 997-999 | `method == "GET"`, `"POST"`, `"DELETE"`, `"PUT"`, `"OPTIONS"` (multiple locations) | Consider constexpr string_view array or use httplib's built-in method enum if available |

### 2.9 Magic Strings - Miscellaneous

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/core/ApiContext.cpp` | 37 | `s == "jpeg" \|\| s == "jpg"` | `constexpr std::string_view kFormatJpeg = "jpeg"` etc. |
| `src/media/MockSfuBackend.cpp` | 279 | `kind == "video"` | `enum class MediaKind { Audio, Video }` |
| `src/api/v1/middleware/CorsMiddleware.cpp` | 37 | `m_cfg.allowedOrigins[0] == "*"` | `constexpr char kCorsWildcard = '*';` |
| `src/core/I18n.cpp` | 59 | `ext != ".json"` | `constexpr std::string_view kJsonExtension = ".json";` |
| `src/api/v2/controllers/BatchController.cpp` | 84-86, 96, 98 | `batchReq.operation == "start"`, `"stop"` | Use `FeatureOperation` enum |

---

## 3. Catch-all Files (helpers.h / utils.h / common.h)

| File | Line | Issue | Solution |
|------|------|-------|----------|
| - | - | **No violations found** | Project properly separates utilities (CryptoUtils, I18n, etc.) |

**Verdict:** Compliant with CLAUDE.md. No catch-all utility files detected.

---

## 4. Raw Strings Used as ID/State/Role -> Type Alias / enum class

### 4.1 Type Aliases (Already Compliant)

The project already defines `using Uid = std::string;` in `include/hub32api/core/Types.hpp:15`.

### 4.2 State/Role Strings (Violations)

These overlap with Section 2.3-2.7 above. Summary of required enum classes:

| Enum Class | Namespace | Values | Files Affected |
|------------|-----------|--------|----------------|
| `UserRole` | `hub32::types` | `Admin, Teacher, Student` | Router.cpp, TeacherController.cpp, SchoolController.cpp |
| `JwtAlgorithm` | `hub32::types` | `RS256, HS256, None` | JwtAuth.cpp, JwtValidator.cpp, ConfigValidator.cpp |
| `FeatureOperation` | `hub32::types` | `Start, Stop` | All 5 agent feature files, BatchController.cpp |
| `PowerAction` | `hub32::types` | `Shutdown, Reboot, Logoff` | PowerControl.cpp |
| `AuthMethod` | `hub32::types` | `Logon, Hub32Key` | AuthController.cpp |
| `FeatureUid` | `hub32::types` | `LockScreen, ScreenBroadcast, InputLock, Message, PowerControl` | FeaturePlugin.cpp |
| `MediaKind` | `hub32::types` | `Audio, Video` | MockSfuBackend.cpp |

---

## 5. Exceptions in Business Logic -> Result<T>

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/config/ServerConfig.cpp` | 54 | `throw std::runtime_error("[ServerConfig] FATAL: OpenSSL RAND_bytes failed")` | Return `Result<std::string>` with `ErrorCode::CryptoFailure` |
| `src/config/internal/ConfigValidator.cpp` | 43 | `throw std::runtime_error(...)` for missing field | Return `Result<void>` with `ErrorCode::InvalidConfig` |
| `src/config/internal/ConfigValidator.cpp` | 50 | `throw std::runtime_error(...)` for missing field | Return `Result<void>` with `ErrorCode::InvalidConfig` |
| `src/config/internal/ConfigValidator.cpp` | 55 | `throw std::runtime_error(...)` for invalid algorithm | Return `Result<void>` with `ErrorCode::InvalidConfig` |
| `src/auth/UserRoleStore.cpp` | 138 | `throw std::runtime_error("RAND_bytes failed for password salt")` | Return `Result<std::string>` with `ErrorCode::CryptoFailure` |
| `src/auth/UserRoleStore.cpp` | 148 | `throw std::runtime_error("PBKDF2_HMAC failed")` | Return `Result<std::string>` with `ErrorCode::CryptoFailure` |
| `src/auth/JwtAuth.cpp` | 135 | `throw std::runtime_error("[JwtAuth] FATAL: RS256 requires keys")` | Return `Result<void>` with `ErrorCode::InvalidConfig` |
| `src/auth/JwtAuth.cpp` | 147 | `throw std::runtime_error("[JwtAuth] FATAL: Cannot read private key")` | Return `Result<void>` with `ErrorCode::FileReadError` |
| `src/auth/JwtAuth.cpp` | 153 | `throw std::runtime_error("[JwtAuth] FATAL: Cannot read public key")` | Return `Result<void>` with `ErrorCode::FileReadError` |
| `src/auth/JwtAuth.cpp` | 162 | `throw std::runtime_error("[JwtAuth] FATAL: HS256 requires secret")` | Return `Result<void>` with `ErrorCode::InvalidConfig` |
| `src/core/CryptoUtils.cpp` | 14 | `throw std::runtime_error("RAND_bytes failed for UUID generation")` | Return `Result<std::string>` with `ErrorCode::CryptoFailure` |
| `src/core/CryptoUtils.cpp` | 31 | `throw std::runtime_error("RAND_bytes failed")` | Return `Result<std::string>` with `ErrorCode::CryptoFailure` |

### Silent Catch Blocks (Error Swallowing)

| File | Line | Issue | Solution |
|------|------|-------|----------|
| `src/api/v1/controllers/FramebufferController.cpp` | 81 | `try { width = std::stoi(...); } catch (...) { width = 0; }` | Use `std::from_chars` (no exceptions) or return validation error |
| `src/api/v1/controllers/FramebufferController.cpp` | 84 | `try { height = std::stoi(...); } catch (...) { height = 0; }` | Same |
| `src/api/v1/controllers/FramebufferController.cpp` | 98 | `try { compression = std::stoi(...); } catch (...) {}` | Same |
| `src/api/v1/controllers/FramebufferController.cpp` | 101 | `try { quality = std::stoi(...); } catch (...) {}` | Same |
| `src/api/v1/controllers/ComputerController.cpp` | 81 | `catch (...) { /* use default */ }` | Use `std::from_chars` or explicit parse with Result |
| `src/core/I18n.cpp` | 238 | `catch (...) {}` (silent swallow) | Log warning, return `Result<void>` |
| `src/auth/UserRoleStore.cpp` | 177 | `catch (...) { return false; }` | Return `Result<bool>` with error detail |

---

## 6. Macros Replaceable by constexpr/inline/template

| File | Line | Issue | Solution |
|------|------|-------|----------|
| - | - | **No violations found** | All macros are legitimate uses (Windows API config, third-party lib activation, CMake export, plugin metadata with `__FILE__`/`__LINE__`) |

**Verdict:** Compliant with CLAUDE.md. All macros fall under acceptable exception categories.

---

## Summary

| Category | Violations | Severity |
|----------|-----------|----------|
| #define -> constexpr | 0 (all legitimate exceptions) | - |
| Magic numbers (time/size) | **22** locations | HIGH |
| Magic strings (role/auth) | **9** locations | CRITICAL |
| Magic strings (JWT algorithm) | **6** locations | HIGH |
| Magic strings (state/command) | **7** locations | MEDIUM |
| Magic strings (feature UID/operation) | **16** locations | HIGH |
| Magic strings (auth method) | **4** locations | CRITICAL |
| Magic strings (HTTP/misc) | **8+** locations | LOW |
| Catch-all util files | 0 | - |
| Raw string IDs | 0 (has `Uid` type alias) | - |
| Exceptions in business logic | **12** throw statements | HIGH |
| Silent error swallowing | **7** catch blocks | MEDIUM |
| Replaceable macros | 0 | - |

### Proposed New Types (hub32::types namespace)

```cpp
enum class UserRole       { Admin, Teacher, Student };
enum class JwtAlgorithm   { RS256, HS256, None };
enum class FeatureOperation { Start, Stop };
enum class PowerAction    { Shutdown, Reboot, Logoff };
enum class AuthMethod     { Logon, Hub32Key };
enum class FeatureUid     { LockScreen, ScreenBroadcast, InputLock, Message, PowerControl };
enum class MediaKind      { Audio, Video };
```

### Proposed Constants File (hub32::constants namespace)

```cpp
// Network
constexpr int kDefaultVncPort      = 11100;  // port number
constexpr int kTcpTimeoutMs        = 1500;   // milliseconds
constexpr int kPingTimeoutMs       = 500;    // milliseconds

// Auth
constexpr int kTokenExpirySec      = 3600;   // seconds (1 hour)
constexpr std::string_view kBearerPrefix = "Bearer ";
constexpr size_t kBearerPrefixLen  = 7;      // bytes

// CORS
constexpr int kCorsMaxAgeSec       = 3600;   // seconds

// Agent
constexpr int kHeartbeatTimeoutMs  = 90000;  // milliseconds (90s)
constexpr int kCommandPollIntervalMs = 5000; // milliseconds

// Service
constexpr int kServicePollIntervalMs = 200;  // milliseconds
constexpr DWORD kServiceStartWaitMs  = 3000; // milliseconds
constexpr DWORD kServiceStopWaitMs   = 5000; // milliseconds

// Buffers
constexpr size_t kSecretKeyBytes   = 32;     // bytes
constexpr size_t kUuidBytes        = 16;     // bytes
constexpr size_t kTimestampBufSize = 32;     // bytes

// Rate Limiting
constexpr int kDefaultRequestsPerMinute = 120; // requests
constexpr int kDefaultBurstSize    = 20;       // requests
constexpr size_t kRateLimitCleanupInterval = 1000; // requests

// Pagination
constexpr int kDefaultPageSize     = 50;     // items
constexpr int kMaxPageSize         = 200;    // items

// Input Validation
constexpr size_t kMaxFieldLength   = 1000;   // characters
constexpr size_t kMaxArraySize     = 500;    // elements
constexpr int kMaxPathDepth        = 10;     // levels

// Cache
constexpr int kCacheMaxAgeSec      = 3600;   // seconds
```
