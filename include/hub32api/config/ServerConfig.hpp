#pragma once

#include <cstdint>
#include <string>
#include "hub32api/export.h"

namespace hub32api {

// -----------------------------------------------------------------------
// ServerConfig — public configuration struct loaded at startup.
// Mirrors Hub32's Hub32Configuration property pattern.
// -----------------------------------------------------------------------
struct HUB32API_EXPORT ServerConfig
{
    // HTTP server
    bool        httpEnabled        = true;
    uint16_t    httpPort           = 11081;   // distinct from Hub32 WebAPI (11080)
    std::string bindAddress        = "127.0.0.1";

    // TLS / HTTPS
    bool        tlsEnabled         = false;
    std::string tlsCertFile;
    std::string tlsKeyFile;

    // Connection management
    int  connectionLimitPerHost    = 4;
    int  globalConnectionLimit     = 64;
    int  connectionLifetimeSec     = 180;
    int  connectionIdleTimeoutSec  = 60;
    int  authTimeoutSec            = 15;

    // Thread pool
    int  workerThreads             = 8;

    // JWT authentication
    std::string jwtSecret;
    int         jwtExpirySeconds   = 3600;
    std::string jwtAlgorithm       = "RS256";  // RS256 (default) or HS256
    std::string jwtPrivateKeyFile;              // PEM private key path (RS256)
    std::string jwtPublicKeyFile;               // PEM public key path (RS256)
    std::string tokenRevocationFile;            // path to persist revoked tokens (empty = in-memory only)

    // User/role store for authentication (replaces hardcoded role assignment)
    std::string usersFile;        // path to users.json for role-based auth

    // SECURITY: File-based key hashes replace env-var key storage.
    // Each file contains a single line: a PBKDF2-SHA256 hash string.
    // Generate with: hub32api-service --hash-password <your-key>
    std::string agentKeyFile;     // path to file containing hashed agent registration key
    std::string authKeyFile;      // path to file containing hashed Hub32 auth key

    // Database
    std::string databaseDir = "data";   // path to SQLite databases

    // Hub32 integration
    std::string hub32PluginDir;   // path to hub32 plugin directory

    // Logging
    std::string logLevel           = "info";  // trace|debug|info|warn|error
    std::string logFile;                      // empty = stdout
    std::string auditLogFile;                 // path to SQLite audit log (empty = disabled)

    // i18n
    std::string localesDir;               // path to locale JSON catalogs
    std::string defaultLocale    = "en";  // fallback locale

    // TURN / ICE
    std::string turnSecret;        // HMAC secret for TURN credential generation (coturn REST API)
    std::string turnServerUrl;     // e.g., "turn.example.com"

    // Metrics
    bool metricsEnabled            = false;
    uint16_t metricsPort           = 9091;

    // Load from JSON file; returns false on parse error
    HUB32API_EXPORT static ServerConfig from_file(const std::string& path);
    HUB32API_EXPORT static ServerConfig from_registry();  // Windows Registry
    HUB32API_EXPORT static ServerConfig defaults();
};

} // namespace hub32api
