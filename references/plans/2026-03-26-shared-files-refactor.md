# Shared Files Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract all magic numbers/strings into constexpr constants and enum classes, convert 12 business-logic throws to Result<T>, and add focused utility modules — all within the existing `hub32api::` namespace.

**Architecture:** Additive changes to existing structure. New `Constants.hpp` in `include/hub32api/core/` for public constants + enums. New utils under `src/utils/`. Existing files modified to use new constants. Build system: CMake with Ninja on Windows/MinGW.

**Tech Stack:** C++17, CMake 3.20+, nlohmann-json, spdlog, OpenSSL, httplib, jwt-cpp, GoogleTest

---

## File Structure

### New files to create:
- `include/hub32api/core/Constants.hpp` — All constexpr constants + new enum classes + to_string/from_string declarations
- `src/core/Constants.cpp` — Implementations of to_string/from_string for new enums
- `src/utils/string_utils.hpp` + `src/utils/string_utils.cpp` — String manipulation pure functions
- `src/utils/time_utils.hpp` + `src/utils/time_utils.cpp` — Time formatting pure functions
- `src/utils/json_utils.hpp` + `src/utils/json_utils.cpp` — Safe JSON accessors (no throw)
- `src/utils/validation_utils.hpp` + `src/utils/validation_utils.cpp` — Input validation pure functions

### Existing files to modify:
- `include/hub32api/core/Error.hpp` — Add missing ErrorCodes + `to_string(ErrorCode)`
- `src/core/CMakeLists.txt` — Add Constants.cpp and utils/*.cpp to hub32api-core target
- `src/core/PrecompiledHeader.hpp` — Add `#include "hub32api/core/Constants.hpp"`
- `src/core/CryptoUtils.cpp` — Convert 2 throws to Result<T>
- `src/core/internal/CryptoUtils.hpp` — Update return types to Result<T>
- `src/auth/JwtAuth.cpp` — Convert 4 throws to Result<T>, use constants
- `src/auth/JwtAuth.hpp` — Update constructor signature
- `src/auth/UserRoleStore.cpp` — Convert 2 throws to Result<T>, extract hex utils
- `src/auth/UserRoleStore.hpp` — Update return type of hashPassword
- `src/config/ServerConfig.cpp` — Convert 1 throw to Result<T>
- `src/config/internal/ConfigValidator.cpp` — Convert 3 throws to Result<T>
- `src/config/internal/ConfigValidator.hpp` — Update validate() return type
- `src/api/v1/controllers/AuthController.cpp` — Use constants for auth methods, Bearer prefix
- `src/api/v1/controllers/TeacherController.cpp` — Use UserRole enum, Bearer constant
- `src/api/v1/controllers/SchoolController.cpp` — Use UserRole enum, Bearer constant
- `src/api/v1/controllers/ComputerController.cpp` — Use pagination constants
- `src/api/v1/controllers/FramebufferController.cpp` — Use image_format_from_string (already exists)
- `src/api/v1/dto/AuthDto.hpp` — Use constants for defaults
- `src/api/v1/middleware/CorsMiddleware.hpp` — Use constants for maxAge
- `src/api/v1/middleware/RateLimitMiddleware.hpp` — Use constants for defaults
- `src/api/v1/middleware/InputValidationMiddleware.hpp` — Use constants for defaults
- `src/auth/internal/JwtValidator.cpp` — Use JwtAlgorithm enum
- `src/plugins/feature/FeaturePlugin.cpp` — Use FeatureUid enum/constants
- `src/agent/HeartbeatMonitor.hpp` — Use constant for default timeout
- `include/hub32api/core/Types.hpp` — Add `feature_operation_from_string()` declaration
- `src/core/ApiContext.cpp` — Add `feature_operation_from_string()` implementation

---

## Task 1: Create Constants.hpp with all constexpr and new enum classes

**Files:**
- Create: `include/hub32api/core/Constants.hpp`
- Create: `src/core/Constants.cpp`
- Modify: `src/core/CMakeLists.txt`
- Modify: `src/core/PrecompiledHeader.hpp`

- [ ] **Step 1: Create `include/hub32api/core/Constants.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include "hub32api/export.h"

namespace hub32api {

// -----------------------------------------------------------------------
// Network constants
// -----------------------------------------------------------------------
constexpr uint16_t kDefaultHttpPort       = 11081;  // port — hub32api HTTP server
constexpr uint16_t kDefaultAgentPort      = 11082;  // port — agent listener
constexpr uint16_t kDefaultVncPort        = 11100;  // port — VNC server
constexpr uint16_t kDefaultMetricsPort    = 9091;   // port — Prometheus metrics
constexpr int      kTcpPingTimeoutMs      = 1500;   // milliseconds — TCP connect timeout
constexpr int      kIcmpPingTimeoutMs     = 500;    // milliseconds — ICMP echo timeout

// -----------------------------------------------------------------------
// Authentication constants
// -----------------------------------------------------------------------
constexpr std::string_view kBearerPrefix   = "Bearer ";
constexpr size_t kBearerPrefixLen          = 7;      // bytes — length of "Bearer "
constexpr int    kDefaultTokenExpirySec    = 3600;   // seconds — 1 hour JWT lifetime
constexpr int    kTokenPurgeIntervalCalls  = 100;    // calls — purge denylist every N authenticate() calls
constexpr std::string_view kJwtIssuer      = "hub32api";
constexpr std::string_view kJwtAudience    = "hub32api-clients";

// Veyon-compatible auth method UUIDs
constexpr std::string_view kAuthMethodHub32KeyUuid = "0c69b301-81b4-42d6-8fae-128cdd113314";
constexpr std::string_view kAuthMethodLogonUuid    = "63611f7c-b457-42c7-832e-67d0f9281085";

// -----------------------------------------------------------------------
// Agent constants
// -----------------------------------------------------------------------
constexpr int    kDefaultHeartbeatTimeoutMs     = 90000;  // milliseconds — 90s agent timeout
constexpr int    kDefaultCommandPollIntervalMs  = 5000;   // milliseconds — agent poll interval
constexpr int    kDefaultHeartbeatIntervalMs    = 30000;  // milliseconds — agent heartbeat interval
constexpr size_t kMaxCommandHistory             = 10000;  // items — command history ring buffer
constexpr size_t kCommandHistoryPruneCount      = 1000;   // items — prune batch size

// -----------------------------------------------------------------------
// Service constants (Windows)
// -----------------------------------------------------------------------
constexpr int kServicePollIntervalMs    = 200;   // milliseconds — service main loop sleep
constexpr int kServiceStartWaitMs       = 3000;  // milliseconds — SCM start pending hint
constexpr int kServiceStopWaitMs        = 5000;  // milliseconds — SCM stop pending hint

// -----------------------------------------------------------------------
// Cache & CORS
// -----------------------------------------------------------------------
constexpr int kDefaultCacheMaxAgeSec   = 3600;   // seconds — Cache-Control max-age
constexpr int kDefaultCorsMaxAgeSec    = 3600;   // seconds — CORS preflight cache

// -----------------------------------------------------------------------
// Rate limiting
// -----------------------------------------------------------------------
constexpr int    kDefaultRequestsPerMinute    = 120;   // requests — rate limit window
constexpr int    kDefaultBurstSize            = 20;    // requests — token bucket burst
constexpr size_t kRateLimitCleanupInterval    = 1000;  // calls — bucket cleanup frequency

// -----------------------------------------------------------------------
// Pagination
// -----------------------------------------------------------------------
constexpr int kDefaultPageSize   = 50;    // items — default page size
constexpr int kMaxPageSize       = 200;   // items — maximum page size

// -----------------------------------------------------------------------
// Input validation
// -----------------------------------------------------------------------
constexpr size_t kDefaultMaxBodySize     = 1 * 1024 * 1024;  // bytes — 1 MB
constexpr size_t kDefaultMaxFieldLength  = 1000;              // characters
constexpr size_t kDefaultMaxArraySize    = 500;               // elements
constexpr int    kDefaultMaxPathDepth    = 10;                // levels

// -----------------------------------------------------------------------
// Crypto / buffer sizes
// -----------------------------------------------------------------------
constexpr size_t kSecretKeyBytes     = 32;   // bytes — JWT secret key length (256 bits)
constexpr size_t kUuidBytes          = 16;   // bytes — UUID v4 raw bytes
constexpr size_t kTimestampBufSize   = 32;   // bytes — ISO 8601 timestamp buffer
constexpr int    kPbkdf2Iterations   = 310000;  // rounds — OWASP 2024 minimum for PBKDF2-SHA256
constexpr int    kPbkdf2SaltBytes    = 16;   // bytes — PBKDF2 salt length
constexpr int    kPbkdf2HashBytes    = 32;   // bytes — PBKDF2 derived key length

// -----------------------------------------------------------------------
// ICMP
// -----------------------------------------------------------------------
constexpr size_t kIcmpReplyPadding   = 32;   // bytes — extra buffer after ICMP_ECHO_REPLY

// -----------------------------------------------------------------------
// Feature UIDs (API-level, prefixed with "feat-")
// -----------------------------------------------------------------------
constexpr std::string_view kFeatureLockScreen     = "feat-lock-screen";
constexpr std::string_view kFeatureScreenBroadcast = "feat-screen-broadcast";
constexpr std::string_view kFeatureInputLock      = "feat-input-lock";
constexpr std::string_view kFeatureMessage        = "feat-message";
constexpr std::string_view kFeaturePowerControl   = "feat-power-control";

// Agent-level handler UIDs (without "feat-" prefix)
constexpr std::string_view kHandlerLockScreen     = "lock-screen";
constexpr std::string_view kHandlerScreenCapture  = "screen-capture";
constexpr std::string_view kHandlerInputLock      = "input-lock";
constexpr std::string_view kHandlerMessageDisplay = "message-display";
constexpr std::string_view kHandlerPowerControl   = "power-control";

// -----------------------------------------------------------------------
// UserRole — role of an authenticated user
// -----------------------------------------------------------------------
enum class UserRole
{
    Admin,
    Teacher,
    Readonly,
    Agent,       // agent registration token role
};

HUB32API_EXPORT std::string to_string(UserRole role);
HUB32API_EXPORT UserRole user_role_from_string(const std::string& s);

// -----------------------------------------------------------------------
// JwtAlgorithm — supported JWT signing algorithms
// -----------------------------------------------------------------------
enum class JwtAlgorithm
{
    RS256,
    HS256,
};

HUB32API_EXPORT std::string to_string(JwtAlgorithm alg);
HUB32API_EXPORT JwtAlgorithm jwt_algorithm_from_string(const std::string& s);

// -----------------------------------------------------------------------
// AuthMethod — authentication method identifiers
// -----------------------------------------------------------------------
enum class AuthMethod
{
    Logon,       // username/password
    Hub32Key,    // Hub32 public key auth
};

HUB32API_EXPORT std::string to_string(AuthMethod m);
HUB32API_EXPORT AuthMethod auth_method_from_string(const std::string& s);

// -----------------------------------------------------------------------
// PowerAction — power control operations
// -----------------------------------------------------------------------
enum class PowerAction
{
    Shutdown,
    Reboot,
    Logoff,
};

HUB32API_EXPORT std::string to_string(PowerAction a);
HUB32API_EXPORT PowerAction power_action_from_string(const std::string& s);

// -----------------------------------------------------------------------
// MediaKind — WebRTC media track types
// -----------------------------------------------------------------------
enum class MediaKind
{
    Audio,
    Video,
};

HUB32API_EXPORT std::string to_string(MediaKind k);
HUB32API_EXPORT MediaKind media_kind_from_string(const std::string& s);

} // namespace hub32api
```

- [ ] **Step 2: Create `src/core/Constants.cpp`**

```cpp
#include "PrecompiledHeader.hpp"
#include "hub32api/core/Constants.hpp"

namespace hub32api {

// -- UserRole ---------------------------------------------------------------
std::string to_string(UserRole role)
{
    switch (role) {
        case UserRole::Admin:    return "admin";
        case UserRole::Teacher:  return "teacher";
        case UserRole::Readonly: return "readonly";
        case UserRole::Agent:    return "agent";
    }
    return "readonly";
}

UserRole user_role_from_string(const std::string& s)
{
    if (s == "admin")    return UserRole::Admin;
    if (s == "teacher")  return UserRole::Teacher;
    if (s == "agent")    return UserRole::Agent;
    return UserRole::Readonly;
}

// -- JwtAlgorithm -----------------------------------------------------------
std::string to_string(JwtAlgorithm alg)
{
    switch (alg) {
        case JwtAlgorithm::RS256: return "RS256";
        case JwtAlgorithm::HS256: return "HS256";
    }
    return "RS256";
}

JwtAlgorithm jwt_algorithm_from_string(const std::string& s)
{
    if (s == "HS256") return JwtAlgorithm::HS256;
    return JwtAlgorithm::RS256;
}

// -- AuthMethod -------------------------------------------------------------
std::string to_string(AuthMethod m)
{
    switch (m) {
        case AuthMethod::Logon:    return "logon";
        case AuthMethod::Hub32Key: return "hub32-key";
    }
    return "logon";
}

AuthMethod auth_method_from_string(const std::string& s)
{
    // Support both string names and Veyon-compatible UUIDs
    if (s == "hub32-key" || s == kAuthMethodHub32KeyUuid) return AuthMethod::Hub32Key;
    if (s == "logon"     || s == kAuthMethodLogonUuid)    return AuthMethod::Logon;
    return AuthMethod::Logon;
}

// -- PowerAction ------------------------------------------------------------
std::string to_string(PowerAction a)
{
    switch (a) {
        case PowerAction::Shutdown: return "shutdown";
        case PowerAction::Reboot:   return "reboot";
        case PowerAction::Logoff:   return "logoff";
    }
    return "shutdown";
}

PowerAction power_action_from_string(const std::string& s)
{
    if (s == "reboot")  return PowerAction::Reboot;
    if (s == "logoff")  return PowerAction::Logoff;
    return PowerAction::Shutdown;
}

// -- MediaKind --------------------------------------------------------------
std::string to_string(MediaKind k)
{
    switch (k) {
        case MediaKind::Audio: return "audio";
        case MediaKind::Video: return "video";
    }
    return "video";
}

MediaKind media_kind_from_string(const std::string& s)
{
    if (s == "audio") return MediaKind::Audio;
    return MediaKind::Video;
}

} // namespace hub32api
```

- [ ] **Step 3: Add Constants.cpp to CMakeLists.txt**

In `src/core/CMakeLists.txt`, add `Constants.cpp` to the `hub32api-core` source list, right after the existing `CryptoUtils.cpp` line:

```
    CryptoUtils.cpp
    Constants.cpp
```

- [ ] **Step 4: Add Constants.hpp to PrecompiledHeader**

In `src/core/PrecompiledHeader.hpp`, add after the existing `#include "hub32api/core/Result.hpp"` line:

```cpp
#include "hub32api/core/Constants.hpp"
```

- [ ] **Step 5: Build to verify**

Run:
```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --preset debug && cmake --build build/debug 2>&1 | tail -20
```
Expected: Build succeeds with 0 errors. New Constants.cpp compiles into hub32api-core.dll.

- [ ] **Step 6: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add include/hub32api/core/Constants.hpp src/core/Constants.cpp src/core/CMakeLists.txt src/core/PrecompiledHeader.hpp
git commit -m "feat: add Constants.hpp with constexpr constants and enum classes

Centralizes all magic numbers/strings into named constexpr constants
grouped by function (Network, Auth, Agent, Cache, Pagination, etc.).
Adds enum classes: UserRole, JwtAlgorithm, AuthMethod, PowerAction,
MediaKind with to_string/from_string conversions.
Every constexpr has a unit comment (ms, bytes, seconds, etc.)."
```

---

## Task 2: Extend Error.hpp with missing error codes and to_string

**Files:**
- Modify: `include/hub32api/core/Error.hpp`
- Modify: `src/core/Constants.cpp` (add to_string(ErrorCode) implementation)

- [ ] **Step 1: Add missing error codes and to_string declaration to Error.hpp**

In `include/hub32api/core/Error.hpp`, add these new entries inside the `ErrorCode` enum, after the existing `FramebufferNotAvailable = 5031` line:

```cpp
    // Crypto errors
    CryptoFailure                 = 5003,    // Maps to HTTP 500

    // Config errors
    InvalidConfig                 = 5004,    // Maps to HTTP 500

    // File errors
    FileReadError                 = 5005,    // Maps to HTTP 500
```

Then add the `to_string` declaration after the `http_status_for` function closing brace:

```cpp
/**
 * @brief Returns a human-readable string for the given ErrorCode.
 */
HUB32API_EXPORT std::string to_string(ErrorCode code);
```

- [ ] **Step 2: Implement to_string(ErrorCode) in Constants.cpp**

Append to the bottom of `src/core/Constants.cpp`, before the closing `} // namespace hub32api`:

```cpp
// -- ErrorCode --------------------------------------------------------------
std::string to_string(ErrorCode code)
{
    switch (code) {
        case ErrorCode::None:                    return "None";
        case ErrorCode::InvalidRequest:          return "InvalidRequest";
        case ErrorCode::InvalidCredentials:      return "InvalidCredentials";
        case ErrorCode::InvalidFeature:          return "InvalidFeature";
        case ErrorCode::InvalidConnection:       return "InvalidConnection";
        case ErrorCode::AuthMethodNotAvailable:  return "AuthMethodNotAvailable";
        case ErrorCode::Unauthorized:            return "Unauthorized";
        case ErrorCode::AuthenticationFailed:    return "AuthenticationFailed";
        case ErrorCode::TokenExpired:            return "TokenExpired";
        case ErrorCode::NotFound:                return "NotFound";
        case ErrorCode::ComputerNotFound:        return "ComputerNotFound";
        case ErrorCode::RequestTimeout:          return "RequestTimeout";
        case ErrorCode::ConnectionTimeout:       return "ConnectionTimeout";
        case ErrorCode::TooManyRequests:         return "TooManyRequests";
        case ErrorCode::ConnectionLimitReached:  return "ConnectionLimitReached";
        case ErrorCode::InternalError:           return "InternalError";
        case ErrorCode::FramebufferEncodingError: return "FramebufferEncodingError";
        case ErrorCode::PluginError:             return "PluginError";
        case ErrorCode::CryptoFailure:           return "CryptoFailure";
        case ErrorCode::InvalidConfig:           return "InvalidConfig";
        case ErrorCode::FileReadError:           return "FileReadError";
        case ErrorCode::NotImplemented:          return "NotImplemented";
        case ErrorCode::ProtocolMismatch:        return "ProtocolMismatch";
        case ErrorCode::ServiceUnavailable:      return "ServiceUnavailable";
        case ErrorCode::FramebufferNotAvailable: return "FramebufferNotAvailable";
    }
    return "Unknown(" + std::to_string(static_cast<int>(code)) + ")";
}
```

- [ ] **Step 3: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add include/hub32api/core/Error.hpp src/core/Constants.cpp
git commit -m "feat: add CryptoFailure, InvalidConfig, FileReadError codes + to_string(ErrorCode)"
```

---

## Task 3: Create string_utils

**Files:**
- Create: `src/utils/string_utils.hpp`
- Create: `src/utils/string_utils.cpp`
- Modify: `src/core/CMakeLists.txt`

- [ ] **Step 1: Create `src/utils/string_utils.hpp`**

```cpp
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace hub32api::utils {

/// @brief Trims leading and trailing whitespace.
std::string trim(std::string_view s);

/// @brief Converts string to lowercase (ASCII only).
std::string to_lower(std::string_view s);

/// @brief Splits a string by delimiter.
std::vector<std::string> split(std::string_view s, char delimiter);

/// @brief Joins strings with a separator.
std::string join(const std::vector<std::string>& parts, std::string_view separator);

/// @brief Checks if string starts with prefix.
bool starts_with(std::string_view s, std::string_view prefix);

/// @brief Checks if string ends with suffix.
bool ends_with(std::string_view s, std::string_view suffix);

/// @brief Converts raw bytes to lowercase hex string.
std::string bytes_to_hex(const unsigned char* data, size_t len);

/// @brief Converts hex string to raw bytes. Returns empty on invalid input.
std::vector<unsigned char> hex_to_bytes(std::string_view hex);

} // namespace hub32api::utils
```

- [ ] **Step 2: Create `src/utils/string_utils.cpp`**

```cpp
#include "string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace hub32api::utils {

std::string trim(std::string_view s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return std::string(s.substr(start, end - start));
}

std::string to_lower(std::string_view s)
{
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::vector<std::string> split(std::string_view s, char delimiter)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == delimiter) {
            parts.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator)
{
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result.append(separator);
        result.append(parts[i]);
    }
    return result;
}

bool starts_with(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string bytes_to_hex(const unsigned char* data, size_t len)
{
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        result.push_back(hex_chars[(data[i] >> 4) & 0x0F]);
        result.push_back(hex_chars[data[i] & 0x0F]);
    }
    return result;
}

std::vector<unsigned char> hex_to_bytes(std::string_view hex)
{
    if (hex.size() % 2 != 0) return {};
    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned char hi = 0, lo = 0;
        char c1 = hex[i], c2 = hex[i + 1];
        if      (c1 >= '0' && c1 <= '9') hi = static_cast<unsigned char>(c1 - '0');
        else if (c1 >= 'a' && c1 <= 'f') hi = static_cast<unsigned char>(c1 - 'a' + 10);
        else if (c1 >= 'A' && c1 <= 'F') hi = static_cast<unsigned char>(c1 - 'A' + 10);
        else return {};
        if      (c2 >= '0' && c2 <= '9') lo = static_cast<unsigned char>(c2 - '0');
        else if (c2 >= 'a' && c2 <= 'f') lo = static_cast<unsigned char>(c2 - 'a' + 10);
        else if (c2 >= 'A' && c2 <= 'F') lo = static_cast<unsigned char>(c2 - 'A' + 10);
        else return {};
        bytes.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return bytes;
}

} // namespace hub32api::utils
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `src/core/CMakeLists.txt`, add after the `Constants.cpp` line:

```
    ../utils/string_utils.cpp
```

- [ ] **Step 4: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/utils/string_utils.hpp src/utils/string_utils.cpp src/core/CMakeLists.txt
git commit -m "feat: add string_utils with trim, split, join, hex conversion"
```

---

## Task 4: Create time_utils

**Files:**
- Create: `src/utils/time_utils.hpp`
- Create: `src/utils/time_utils.cpp`
- Modify: `src/core/CMakeLists.txt`

- [ ] **Step 1: Create `src/utils/time_utils.hpp`**

```cpp
#pragma once

#include <string>
#include <chrono>
#include <cstdint>

namespace hub32api::utils {

/// @brief Returns current time as Unix epoch seconds.
int64_t now_unix();

/// @brief Returns current time as Unix epoch milliseconds.
int64_t now_unix_ms();

/// @brief Formats a time_point as ISO 8601 string (e.g., "2026-03-26T10:30:00Z").
std::string format_iso8601(std::chrono::system_clock::time_point tp);

/// @brief Formats current time as ISO 8601 string.
std::string format_iso8601_now();

} // namespace hub32api::utils
```

- [ ] **Step 2: Create `src/utils/time_utils.cpp`**

```cpp
#include "time_utils.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace hub32api::utils {

int64_t now_unix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t now_unix_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string format_iso8601(std::chrono::system_clock::time_point tp)
{
    const auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val{};
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string format_iso8601_now()
{
    return format_iso8601(std::chrono::system_clock::now());
}

} // namespace hub32api::utils
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `src/core/CMakeLists.txt`, add after `../utils/string_utils.cpp`:

```
    ../utils/time_utils.cpp
```

- [ ] **Step 4: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/utils/time_utils.hpp src/utils/time_utils.cpp src/core/CMakeLists.txt
git commit -m "feat: add time_utils with now_unix, format_iso8601"
```

---

## Task 5: Create json_utils

**Files:**
- Create: `src/utils/json_utils.hpp`
- Create: `src/utils/json_utils.cpp`
- Modify: `src/core/CMakeLists.txt`

- [ ] **Step 1: Create `src/utils/json_utils.hpp`**

```cpp
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json_fwd.hpp>

namespace hub32api::utils {

/// @brief Safely gets a string from JSON. Returns nullopt if key missing or wrong type.
std::optional<std::string> safe_get_string(const nlohmann::json& j, const std::string& key);

/// @brief Safely gets an int from JSON. Returns nullopt if key missing or wrong type.
std::optional<int> safe_get_int(const nlohmann::json& j, const std::string& key);

/// @brief Safely gets a bool from JSON. Returns nullopt if key missing or wrong type.
std::optional<bool> safe_get_bool(const nlohmann::json& j, const std::string& key);

/// @brief Checks that all specified fields exist in the JSON object.
/// @return List of missing field names (empty if all present).
std::vector<std::string> missing_fields(const nlohmann::json& j,
                                         const std::vector<std::string>& required);

} // namespace hub32api::utils
```

- [ ] **Step 2: Create `src/utils/json_utils.cpp`**

```cpp
#include "json_utils.hpp"

#include <nlohmann/json.hpp>

namespace hub32api::utils {

std::optional<std::string> safe_get_string(const nlohmann::json& j, const std::string& key)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return std::nullopt;
    return j[key].get<std::string>();
}

std::optional<int> safe_get_int(const nlohmann::json& j, const std::string& key)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_number_integer()) return std::nullopt;
    return j[key].get<int>();
}

std::optional<bool> safe_get_bool(const nlohmann::json& j, const std::string& key)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_boolean()) return std::nullopt;
    return j[key].get<bool>();
}

std::vector<std::string> missing_fields(const nlohmann::json& j,
                                         const std::vector<std::string>& required)
{
    std::vector<std::string> missing;
    if (!j.is_object()) return required;
    for (const auto& field : required) {
        if (!j.contains(field)) missing.push_back(field);
    }
    return missing;
}

} // namespace hub32api::utils
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `src/core/CMakeLists.txt`, add after `../utils/time_utils.cpp`:

```
    ../utils/json_utils.cpp
```

- [ ] **Step 4: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/utils/json_utils.hpp src/utils/json_utils.cpp src/core/CMakeLists.txt
git commit -m "feat: add json_utils with safe getters and missing_fields"
```

---

## Task 6: Create validation_utils

**Files:**
- Create: `src/utils/validation_utils.hpp`
- Create: `src/utils/validation_utils.cpp`
- Modify: `src/core/CMakeLists.txt`

- [ ] **Step 1: Create `src/utils/validation_utils.hpp`**

```cpp
#pragma once

#include <string>
#include <string_view>

namespace hub32api::utils {

/// @brief Validates a username: 1-64 chars, alphanumeric + underscore + hyphen.
bool validate_username(std::string_view username);

/// @brief Validates a UUID-format ID string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).
bool validate_id(std::string_view id);

/// @brief Strips HTML tags and trims whitespace. Returns sanitized string.
std::string sanitize_input(std::string_view input);

/// @brief Checks that a string length is within [minLen, maxLen].
bool check_length(std::string_view s, size_t minLen, size_t maxLen);

} // namespace hub32api::utils
```

- [ ] **Step 2: Create `src/utils/validation_utils.cpp`**

```cpp
#include "validation_utils.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace hub32api::utils {

bool validate_username(std::string_view username)
{
    if (username.empty() || username.size() > 64) return false;
    return std::all_of(username.begin(), username.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.';
    });
}

bool validate_id(std::string_view id)
{
    // UUID v4 format: 8-4-4-4-12 hex chars
    if (id.size() != 36) return false;
    for (size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') return false;
        } else {
            const char c = id[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return false;
        }
    }
    return true;
}

std::string sanitize_input(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    bool inTag = false;
    for (char c : input) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; continue; }
        if (!inTag) result.push_back(c);
    }
    // Trim
    size_t start = 0;
    while (start < result.size() && std::isspace(static_cast<unsigned char>(result[start]))) ++start;
    size_t end = result.size();
    while (end > start && std::isspace(static_cast<unsigned char>(result[end - 1]))) --end;
    return result.substr(start, end - start);
}

bool check_length(std::string_view s, size_t minLen, size_t maxLen)
{
    return s.size() >= minLen && s.size() <= maxLen;
}

} // namespace hub32api::utils
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `src/core/CMakeLists.txt`, add after `../utils/json_utils.cpp`:

```
    ../utils/validation_utils.cpp
```

- [ ] **Step 4: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/utils/validation_utils.hpp src/utils/validation_utils.cpp src/core/CMakeLists.txt
git commit -m "feat: add validation_utils with validate_username, validate_id, sanitize_input"
```

---

## Task 7: Convert CryptoUtils throws to Result<T>

**Files:**
- Modify: `src/core/internal/CryptoUtils.hpp`
- Modify: `src/core/CryptoUtils.cpp`

- [ ] **Step 1: Update CryptoUtils.hpp return types**

Replace the entire file content of `src/core/internal/CryptoUtils.hpp`:

```cpp
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

namespace hub32api::core::internal {

/// @brief Cryptographic utilities using OpenSSL CSPRNG.
/// All methods return Result<T> instead of throwing on failure.
class HUB32API_EXPORT CryptoUtils
{
public:
    /// @brief Generates a RFC 4122 v4 UUID string.
    /// @return Result containing UUID string, or CryptoFailure error.
    static Result<std::string> generateUuid();

    /// @brief Generates cryptographically secure random bytes.
    /// @param count Number of random bytes to generate.
    /// @return Result containing byte vector, or CryptoFailure error.
    static Result<std::vector<uint8_t>> randomBytes(size_t count);

    CryptoUtils() = delete;
};

} // namespace hub32api::core::internal
```

- [ ] **Step 2: Update CryptoUtils.cpp to return Result<T>**

Replace the entire file content of `src/core/CryptoUtils.cpp`:

```cpp
#include "PrecompiledHeader.hpp"
#include "internal/CryptoUtils.hpp"
#include "hub32api/core/Constants.hpp"
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>

namespace hub32api::core::internal {

Result<std::string> CryptoUtils::generateUuid()
{
    uint8_t bytes[kUuidBytes];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::CryptoFailure, "RAND_bytes failed for UUID generation"
        });
    }
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < kUuidBytes; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return Result<std::string>::ok(oss.str());
}

Result<std::vector<uint8_t>> CryptoUtils::randomBytes(size_t count)
{
    std::vector<uint8_t> buf(count);
    if (count > 0 && RAND_bytes(buf.data(), static_cast<int>(count)) != 1) {
        return Result<std::vector<uint8_t>>::fail(ApiError{
            ErrorCode::CryptoFailure, "RAND_bytes failed"
        });
    }
    return Result<std::vector<uint8_t>>::ok(std::move(buf));
}

} // namespace hub32api::core::internal
```

- [ ] **Step 3: Build to verify — expect errors**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | head -40
```
Expected: Compilation errors in files that call `CryptoUtils::generateUuid()` and `CryptoUtils::randomBytes()` — they now return `Result<T>` instead of raw values. We will fix these callers in Tasks 8-9.

- [ ] **Step 4: Fix caller in JwtAuth.cpp (line 211)**

In `src/auth/JwtAuth.cpp`, find line `const std::string jti = core::internal::CryptoUtils::generateUuid();` and replace with:

```cpp
        auto uuidResult = core::internal::CryptoUtils::generateUuid();
        if (uuidResult.is_err()) {
            return Result<std::string>::fail(ApiError{
                ErrorCode::CryptoFailure, "Failed to generate token ID"
            });
        }
        const std::string jti = uuidResult.take();
```

- [ ] **Step 5: Fix caller in AgentRegistry.cpp**

Search `src/agent/AgentRegistry.cpp` for calls to `CryptoUtils::generateUuid()` and wrap each in Result handling. The pattern is the same — replace `const auto id = CryptoUtils::generateUuid();` with:

```cpp
    auto uuidResult = core::internal::CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        return Result<Uid>::fail(uuidResult.error());
    }
    const auto id = uuidResult.take();
```

- [ ] **Step 6: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```
Expected: Build succeeds. All callers of CryptoUtils now handle Result<T>.

- [ ] **Step 7: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/core/internal/CryptoUtils.hpp src/core/CryptoUtils.cpp src/auth/JwtAuth.cpp src/agent/AgentRegistry.cpp
git commit -m "refactor: CryptoUtils returns Result<T> instead of throwing

generateUuid() and randomBytes() now return Result<T> with
ErrorCode::CryptoFailure instead of throwing std::runtime_error.
All callers updated to handle the Result."
```

---

## Task 8: Convert auth throws to Result<T>

**Files:**
- Modify: `src/auth/JwtAuth.cpp`
- Modify: `src/auth/JwtAuth.hpp`
- Modify: `src/auth/UserRoleStore.cpp`
- Modify: `src/auth/UserRoleStore.hpp`

- [ ] **Step 1: Change JwtAuth constructor to return Result via factory**

In `src/auth/JwtAuth.hpp`, change the constructor to private and add a static factory:

Add this public static method declaration:

```cpp
    /// @brief Creates a JwtAuth instance. Returns error if keys/secret are invalid.
    static Result<std::unique_ptr<JwtAuth>> create(const ServerConfig& cfg);
```

- [ ] **Step 2: Implement factory in JwtAuth.cpp**

Replace the JwtAuth constructor body (lines 109-173) to be a private constructor that takes an already-validated Impl, and implement the `create()` factory that does the validation and returns `Result<std::unique_ptr<JwtAuth>>`. The 4 throws become `Result::fail()` returns.

The constructor becomes:

```cpp
JwtAuth::JwtAuth(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
```

And the factory:

```cpp
Result<std::unique_ptr<JwtAuth>> JwtAuth::create(const ServerConfig& cfg)
{
    auto impl = std::make_unique<Impl>();
    impl->algorithm     = cfg.jwtAlgorithm;
    impl->secret        = cfg.jwtSecret;
    impl->expirySeconds = cfg.jwtExpirySeconds;

    if (cfg.jwtAlgorithm == to_string(JwtAlgorithm::RS256)) {
        if (cfg.jwtPrivateKeyFile.empty() || cfg.jwtPublicKeyFile.empty()) {
            return Result<std::unique_ptr<JwtAuth>>::fail(ApiError{
                ErrorCode::InvalidConfig,
                "[JwtAuth] RS256 algorithm requires both private and public key files"
            });
        }
        impl->privateKey = readFileContent(cfg.jwtPrivateKeyFile);
        impl->publicKey  = readFileContent(cfg.jwtPublicKeyFile);
        if (impl->privateKey.empty()) {
            return Result<std::unique_ptr<JwtAuth>>::fail(ApiError{
                ErrorCode::FileReadError,
                "[JwtAuth] Cannot read RS256 private key file: " + cfg.jwtPrivateKeyFile
            });
        }
        if (impl->publicKey.empty()) {
            return Result<std::unique_ptr<JwtAuth>>::fail(ApiError{
                ErrorCode::FileReadError,
                "[JwtAuth] Cannot read RS256 public key file: " + cfg.jwtPublicKeyFile
            });
        }
    }

    if (cfg.jwtAlgorithm == to_string(JwtAlgorithm::HS256) && cfg.jwtSecret.empty()) {
        return Result<std::unique_ptr<JwtAuth>>::fail(ApiError{
            ErrorCode::InvalidConfig,
            "[JwtAuth] HS256 algorithm requires a non-empty jwtSecret"
        });
    }

    impl->validator = std::make_unique<internal::JwtValidator>(
        impl->algorithm, impl->secret, impl->publicKey);
    impl->store = std::make_unique<internal::TokenStore>(cfg.tokenRevocationFile);

    spdlog::info("[JwtAuth] using {} algorithm", impl->algorithm);

    // Use private constructor
    auto auth = std::unique_ptr<JwtAuth>(new JwtAuth(std::move(impl)));
    return Result<std::unique_ptr<JwtAuth>>::ok(std::move(auth));
}
```

- [ ] **Step 3: Update UserRoleStore::hashPassword to return Result**

In `src/auth/UserRoleStore.hpp`, change:
```cpp
    static std::string hashPassword(const std::string& password);
```
to:
```cpp
    static Result<std::string> hashPassword(const std::string& password);
```

In `src/auth/UserRoleStore.cpp`, update the implementation to use `utils::bytes_to_hex` and return Result:

```cpp
Result<std::string> UserRoleStore::hashPassword(const std::string& password)
{
    unsigned char salt[kPbkdf2SaltBytes];
    if (RAND_bytes(salt, kPbkdf2SaltBytes) != 1) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::CryptoFailure, "RAND_bytes failed for password salt"
        });
    }

    unsigned char hash[kPbkdf2HashBytes];
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt, kPbkdf2SaltBytes,
            kPbkdf2Iterations,
            EVP_sha256(),
            kPbkdf2HashBytes, hash) != 1) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::CryptoFailure, "PBKDF2_HMAC failed"
        });
    }

    return Result<std::string>::ok(
        "$pbkdf2-sha256$" + std::to_string(kPbkdf2Iterations) + "$" +
        utils::bytes_to_hex(salt, kPbkdf2SaltBytes) + "$" +
        utils::bytes_to_hex(hash, kPbkdf2HashBytes));
}
```

Also add `#include "utils/string_utils.hpp"` at the top and replace inline `bytesToHex` / `hexToBytes` calls with `utils::bytes_to_hex` / `utils::hex_to_bytes`.

- [ ] **Step 4: Update all callers of JwtAuth constructor and hashPassword**

Search for `JwtAuth(cfg)` and `JwtAuth::hashPassword(` calls across the codebase. Each constructor call becomes `JwtAuth::create(cfg)` with Result handling. Each `hashPassword` call handles the Result.

- [ ] **Step 5: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -30
```
Expected: Build succeeds after all callers are updated.

- [ ] **Step 6: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/auth/JwtAuth.hpp src/auth/JwtAuth.cpp src/auth/UserRoleStore.hpp src/auth/UserRoleStore.cpp
git commit -m "refactor: JwtAuth/UserRoleStore use Result<T> instead of throwing

JwtAuth::create() factory replaces throwing constructor.
UserRoleStore::hashPassword() returns Result<string>.
All 6 throws converted to Result::fail()."
```

---

## Task 9: Convert config throws to Result<T>

**Files:**
- Modify: `src/config/internal/ConfigValidator.cpp`
- Modify: `src/config/internal/ConfigValidator.hpp`
- Modify: `src/config/ServerConfig.cpp`

- [ ] **Step 1: Update ConfigValidator to return Result<void>**

In `src/config/internal/ConfigValidator.hpp`, change the validate method signature from:
```cpp
    std::vector<std::string> validate(const ServerConfig& cfg) const;
```
to:
```cpp
    /// @brief Validates critical config fields. Returns error for fatal issues.
    /// @return Result<std::vector<std::string>> — ok with warnings list, or fail for fatal errors.
    Result<std::vector<std::string>> validate(const ServerConfig& cfg) const;
```

- [ ] **Step 2: Update ConfigValidator.cpp — replace throws with Result::fail**

Replace the 3 throws in `src/config/internal/ConfigValidator.cpp` with `return Result::fail(...)`:

```cpp
Result<std::vector<std::string>> ConfigValidator::validate(const ServerConfig& cfg) const
{
    std::vector<std::string> errors;

    if (cfg.httpPort == 0) {
        return Result<std::vector<std::string>>::fail(ApiError{
            ErrorCode::InvalidConfig,
            "httpPort must be in the range 1-65535 (got " + std::to_string(cfg.httpPort) + ")"
        });
    }

    if (cfg.bindAddress.empty()) {
        return Result<std::vector<std::string>>::fail(ApiError{
            ErrorCode::InvalidConfig, "bindAddress must not be empty"
        });
    }

    if (cfg.jwtAlgorithm != to_string(JwtAlgorithm::RS256) &&
        cfg.jwtAlgorithm != to_string(JwtAlgorithm::HS256)) {
        return Result<std::vector<std::string>>::fail(ApiError{
            ErrorCode::InvalidConfig,
            "jwtAlgorithm must be \"RS256\" or \"HS256\" (got \"" + cfg.jwtAlgorithm + "\")"
        });
    }

    // ... rest of non-critical checks unchanged, building up errors vector ...

    return Result<std::vector<std::string>>::ok(std::move(errors));
}
```

- [ ] **Step 3: Update ServerConfig.cpp — handle Result from validator and generateRandomSecret**

In `src/config/ServerConfig.cpp`, replace `throw std::runtime_error(...)` in `generateRandomSecret()` with returning empty string and logging FATAL, or change it to return `Result<std::string>`. Since this is only called within ServerConfig loading, change to `Result<std::string>`:

```cpp
Result<std::string> generateRandomSecret()
{
    unsigned char buf[kSecretKeyBytes];
    if (RAND_bytes(buf, sizeof(buf)) != 1) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::CryptoFailure,
            "[ServerConfig] OpenSSL RAND_bytes failed — cannot generate JWT secret"
        });
    }
    return Result<std::string>::ok(utils::bytes_to_hex(buf, kSecretKeyBytes));
}
```

Update callers of `generateRandomSecret()` and `validator.validate(cfg)` to handle Result<T>.

- [ ] **Step 4: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/config/internal/ConfigValidator.hpp src/config/internal/ConfigValidator.cpp src/config/ServerConfig.cpp
git commit -m "refactor: ConfigValidator and ServerConfig use Result<T> instead of throwing

3 throws in ConfigValidator + 1 throw in generateRandomSecret()
converted to Result::fail(). Uses ErrorCode::InvalidConfig and
ErrorCode::CryptoFailure."
```

---

## Task 10: Replace magic strings in controllers with constants/enums

**Files:**
- Modify: `src/api/v1/controllers/AuthController.cpp`
- Modify: `src/api/v1/controllers/TeacherController.cpp`
- Modify: `src/api/v1/controllers/SchoolController.cpp`
- Modify: `src/api/v1/controllers/ComputerController.cpp`
- Modify: `src/api/v1/dto/AuthDto.hpp`

- [ ] **Step 1: Update AuthController.cpp**

Replace `normalizeAuthMethod` function:

```cpp
hub32api::AuthMethod normalizeAuthMethod(const std::string& method)
{
    return hub32api::auth_method_from_string(method);
}
```

Replace role string assignments with `to_string(UserRole::Teacher)` etc.

Replace `authHeader.rfind("Bearer ", 0) != 0` with:
```cpp
if (authHeader.empty() || !hub32api::utils::starts_with(authHeader, hub32api::kBearerPrefix))
```

Replace `authHeader.substr(7)` with `authHeader.substr(kBearerPrefixLen)`.

Replace `3600` in AuthResponse with `kDefaultTokenExpirySec`.

Replace `req_dto.method == "logon"` with `normalizeAuthMethod(req_dto.method) == AuthMethod::Logon` etc.

- [ ] **Step 2: Update TeacherController.cpp and SchoolController.cpp**

In both files, replace the `requireAdmin` helper:

```cpp
bool requireAdmin(const httplib::Request& req, httplib::Response& res,
                  hub32api::auth::JwtAuth& jwtAuth, const std::string& lang)
{
    using hub32api::core::internal::tr;
    const std::string authHeader = req.get_header_value("Authorization");
    if (authHeader.size() <= hub32api::kBearerPrefixLen) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return false;
    }
    const std::string token = authHeader.substr(hub32api::kBearerPrefixLen);
    auto result = jwtAuth.authenticate(token);
    if (result.is_err() || !result.value().token ||
        hub32api::user_role_from_string(result.value().token->role) != hub32api::UserRole::Admin) {
        sendError(res, 403, tr(lang, "error.forbidden"));
        return false;
    }
    return true;
}
```

- [ ] **Step 3: Update ComputerController.cpp**

Replace `int limit = 50;` with `int limit = kDefaultPageSize;` and `std::clamp(limit, 1, 200)` with `std::clamp(limit, 1, kMaxPageSize)`.

- [ ] **Step 4: Update AuthDto.hpp**

Replace hardcoded defaults:
```cpp
struct AuthResponse
{
    std::string token;
    std::string tokenType = "Bearer";
    int         expiresIn = hub32api::kDefaultTokenExpirySec;  // seconds
};
```

Note: `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE` macro requires non-dependent default values for some compilers. If build fails, keep the `= 3600` literal but add a comment `// kDefaultTokenExpirySec`.

- [ ] **Step 5: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 6: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/api/v1/controllers/AuthController.cpp src/api/v1/controllers/TeacherController.cpp src/api/v1/controllers/SchoolController.cpp src/api/v1/controllers/ComputerController.cpp src/api/v1/dto/AuthDto.hpp
git commit -m "refactor: controllers use Constants.hpp enums and constexpr

Replace magic strings 'admin', 'Bearer ', 'logon', 'hub32-key',
auth method UUIDs, and magic numbers 50/200/3600/7 with named
constants and enum classes from Constants.hpp."
```

---

## Task 11: Replace magic strings in middleware and config

**Files:**
- Modify: `src/api/v1/middleware/CorsMiddleware.hpp`
- Modify: `src/api/v1/middleware/RateLimitMiddleware.hpp`
- Modify: `src/api/v1/middleware/InputValidationMiddleware.hpp`
- Modify: `src/auth/internal/JwtValidator.cpp`

- [ ] **Step 1: Update middleware default values**

In `CorsMiddleware.hpp`:
```cpp
    int  maxAgeSec = hub32api::kDefaultCorsMaxAgeSec;  // seconds
```

In `RateLimitMiddleware.hpp`:
```cpp
struct RateLimitConfig {
    int requestsPerMinute = hub32api::kDefaultRequestsPerMinute;  // requests
    int burstSize         = hub32api::kDefaultBurstSize;          // requests
};
```

In `InputValidationMiddleware.hpp`:
```cpp
struct ValidationConfig
{
    size_t maxBodySize       = hub32api::kDefaultMaxBodySize;      // bytes — 1 MB
    size_t maxFieldLength    = hub32api::kDefaultMaxFieldLength;   // characters
    size_t maxArraySize      = hub32api::kDefaultMaxArraySize;     // elements
    int    maxPathDepth      = hub32api::kDefaultMaxPathDepth;     // levels
};
```

- [ ] **Step 2: Update JwtValidator.cpp**

Replace the local `k_issuer`/`k_audience` constants with `kJwtIssuer`/`kJwtAudience` from Constants.hpp.

Replace `tokenAlg == "none" || tokenAlg == "None" || tokenAlg == "NONE"` with:
```cpp
auto lowerAlg = hub32api::utils::to_lower(tokenAlg);
if (lowerAlg == "none") {
```

Replace `m_algorithm == "RS256"` with:
```cpp
if (m_algorithm == to_string(JwtAlgorithm::RS256)) {
```

- [ ] **Step 3: Update RateLimitMiddleware.cpp**

Replace `m_callCount % 1000 == 0` with `m_callCount % kRateLimitCleanupInterval == 0`.

- [ ] **Step 4: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/api/v1/middleware/CorsMiddleware.hpp src/api/v1/middleware/RateLimitMiddleware.hpp src/api/v1/middleware/InputValidationMiddleware.hpp src/api/v1/middleware/RateLimitMiddleware.cpp src/auth/internal/JwtValidator.cpp
git commit -m "refactor: middleware and JwtValidator use named constants

Replace magic numbers in CorsMiddleware (3600), RateLimitMiddleware
(120, 20, 1000), InputValidationMiddleware (1MB, 1000, 500, 10),
and JwtValidator algorithm strings with Constants.hpp values."
```

---

## Task 12: Replace magic strings in FeaturePlugin and agent code

**Files:**
- Modify: `src/plugins/feature/FeaturePlugin.cpp`
- Modify: `src/agent/HeartbeatMonitor.hpp`
- Modify: `src/api/v1/middleware/AuthMiddleware.cpp`

- [ ] **Step 1: Update FeaturePlugin.cpp**

Replace `agentFeatureUid()` method:

```cpp
std::string FeaturePlugin::agentFeatureUid(const Uid& featureUid)
{
    if (featureUid == kFeatureLockScreen)      return std::string(kHandlerLockScreen);
    if (featureUid == kFeatureScreenBroadcast) return std::string(kHandlerScreenCapture);
    if (featureUid == kFeatureInputLock)       return std::string(kHandlerInputLock);
    if (featureUid == kFeatureMessage)         return std::string(kHandlerMessageDisplay);
    if (featureUid == kFeaturePowerControl)    return std::string(kHandlerPowerControl);
    return featureUid;
}
```

Replace feature catalog string literals in `listFeatures()` with the same constants.

- [ ] **Step 2: Update HeartbeatMonitor.hpp**

Replace `std::chrono::milliseconds m_timeout{90000};` with:
```cpp
    std::chrono::milliseconds  m_timeout{kDefaultHeartbeatTimeoutMs};  // milliseconds
```

- [ ] **Step 3: Update AuthMiddleware.cpp**

Replace `authHeader.rfind("Bearer ", 0) != 0` with:
```cpp
if (!hub32api::utils::starts_with(authHeader, std::string(kBearerPrefix))) {
```

Note: `starts_with` takes `string_view`, and `kBearerPrefix` is already `string_view`, so this works directly.

- [ ] **Step 4: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/plugins/feature/FeaturePlugin.cpp src/agent/HeartbeatMonitor.hpp src/api/v1/middleware/AuthMiddleware.cpp
git commit -m "refactor: FeaturePlugin, HeartbeatMonitor, AuthMiddleware use constants

Replace feature UID strings, heartbeat timeout magic number,
and Bearer prefix string with Constants.hpp values."
```

---

## Task 13: Replace magic strings in JwtAuth.cpp

**Files:**
- Modify: `src/auth/JwtAuth.cpp`

- [ ] **Step 1: Replace local constants with Constants.hpp**

Remove the anonymous namespace constants:
```cpp
// DELETE these lines:
    constexpr const char* k_issuer   = "hub32api";
    constexpr const char* k_audience = "hub32api-clients";
```

Replace all uses of `k_issuer` with `std::string(kJwtIssuer)` and `k_audience` with `std::string(kJwtAudience)`.

Replace `m_impl->algorithm == "RS256"` with `m_impl->algorithm == to_string(JwtAlgorithm::RS256)`.

Replace `authCount % 100` with `authCount % kTokenPurgeIntervalCalls`.

- [ ] **Step 2: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 3: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/auth/JwtAuth.cpp
git commit -m "refactor: JwtAuth uses Constants.hpp for issuer, audience, algorithm"
```

---

## Task 14: Replace magic numbers in UserRoleStore.cpp with constants

**Files:**
- Modify: `src/auth/UserRoleStore.cpp`

- [ ] **Step 1: Replace local PBKDF2 constants**

Remove the anonymous namespace constants:
```cpp
// DELETE these lines:
    constexpr int PBKDF2_ITERATIONS = 310000;
    constexpr int SALT_BYTES = 16;
    constexpr int HASH_BYTES = 32;
```

Replace all references:
- `PBKDF2_ITERATIONS` -> `kPbkdf2Iterations`
- `SALT_BYTES` -> `kPbkdf2SaltBytes`
- `HASH_BYTES` -> `kPbkdf2HashBytes`

Also replace inline `bytesToHex` / `hexToBytes` with `utils::bytes_to_hex` / `utils::hex_to_bytes` and add `#include "utils/string_utils.hpp"`.

- [ ] **Step 2: Build to verify**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api && cmake --build build/debug 2>&1 | tail -20
```

- [ ] **Step 3: Commit**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add src/auth/UserRoleStore.cpp
git commit -m "refactor: UserRoleStore uses Constants.hpp for PBKDF2 params and string_utils"
```

---

## Task 15: Final build and test verification

**Files:** None (verification only)

- [ ] **Step 1: Full clean rebuild**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
rm -rf build/debug
cmake --preset debug
cmake --build build/debug 2>&1 | tail -30
```
Expected: 0 errors, 0 warnings related to our changes.

- [ ] **Step 2: Run tests (if BUILD_TESTS is enabled)**

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
cmake --preset debug -DBUILD_TESTS=ON
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure 2>&1
```
Expected: All tests pass.

- [ ] **Step 3: Verify no remaining magic strings**

Run a quick grep to confirm major magic strings are gone:

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
grep -rn '"admin"' src/ --include='*.cpp' | grep -v 'test_' | grep -v 'Constants.cpp'
grep -rn '"Bearer "' src/ --include='*.cpp' | grep -v 'Constants.cpp'
grep -rn 'substr(7)' src/ --include='*.cpp'
grep -rn '== "RS256"' src/ --include='*.cpp' | grep -v 'Constants.cpp'
grep -rn '= 3600' src/ --include='*.hpp'
```
Expected: No matches (all replaced with constants).

- [ ] **Step 4: Final commit**

If any fixes were needed, commit them:

```bash
cd /c/Users/Admin/Desktop/veyon/hub32api
git add -A
git commit -m "fix: address remaining magic strings found during verification"
```
