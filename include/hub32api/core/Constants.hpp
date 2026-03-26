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
constexpr std::string_view kFeatureLockScreen      = "feat-lock-screen";
constexpr std::string_view kFeatureScreenBroadcast = "feat-screen-broadcast";
constexpr std::string_view kFeatureInputLock       = "feat-input-lock";
constexpr std::string_view kFeatureMessage         = "feat-message";
constexpr std::string_view kFeaturePowerControl    = "feat-power-control";

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
