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

    // Hub32 integration
    std::string hub32PluginDir;   // path to hub32 plugin directory

    // Logging
    std::string logLevel           = "info";  // trace|debug|info|warn|error
    std::string logFile;                      // empty = stdout
    std::string auditLogFile;                 // path to SQLite audit log (empty = disabled)

    // Metrics
    bool metricsEnabled            = false;
    uint16_t metricsPort           = 9091;

    // Load from JSON file; returns false on parse error
    HUB32API_EXPORT static ServerConfig from_file(const std::string& path);
    HUB32API_EXPORT static ServerConfig from_registry();  // Windows Registry
    HUB32API_EXPORT static ServerConfig defaults();
};

} // namespace hub32api
