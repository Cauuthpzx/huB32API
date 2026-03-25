#pragma once

#include <cstdint>
#include <string>
#include "veyon32api/export.h"

namespace veyon32api {

// -----------------------------------------------------------------------
// ServerConfig — public configuration struct loaded at startup.
// Mirrors Veyon's VeyonConfiguration property pattern.
// -----------------------------------------------------------------------
struct VEYON32API_EXPORT ServerConfig
{
    // HTTP server
    bool        httpEnabled        = true;
    uint16_t    httpPort           = 11081;   // distinct from Veyon WebAPI (11080)
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

    // Veyon integration
    std::string veyonPluginDir;   // path to veyon plugin directory

    // Logging
    std::string logLevel           = "info";  // trace|debug|info|warn|error
    std::string logFile;                      // empty = stdout

    // Metrics
    bool metricsEnabled            = false;
    uint16_t metricsPort           = 9091;

    // Load from JSON file; returns false on parse error
    VEYON32API_EXPORT static ServerConfig from_file(const std::string& path);
    VEYON32API_EXPORT static ServerConfig from_registry();  // Windows Registry
    VEYON32API_EXPORT static ServerConfig defaults();
};

} // namespace veyon32api
