#pragma once

#include <cstdint>
#include <string>
#include "hub32api/export.h"
#include "hub32api/core/Result.hpp"

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
    std::string jwtAlgorithm       = "RS256";  // RS256 only (HS256 removed)
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

    // SFU backend selection
    std::string sfuBackend         = "mock";  // "mock" or "mediasoup"
    int         sfuWorkerCount     = 0;       // 0 = auto (CPU cores); used by mediasoup backend
    int         rtcMinPort         = 40000;   // RTC relay port range minimum
    int         rtcMaxPort         = 49999;   // RTC relay port range maximum

    // TURN / ICE
    std::string turnSecret;        // HMAC secret for TURN credential generation (coturn REST API)
    std::string turnServerUrl;     // e.g., "turn.example.com"

    // Metrics
    bool metricsEnabled            = false;
    uint16_t metricsPort           = 9091;

    // SMTP — gửi email xác thực đăng ký (Phase 8)
    std::string smtpHost;                          // địa chỉ SMTP server (vd: smtp.gmail.com)
    uint16_t    smtpPort           = 587;          // cổng SMTP: 587 (STARTTLS) hoặc 465 (SSL)
    bool        smtpUseTls         = true;         // true = STARTTLS/SSL, false = plain text
    std::string smtpUsername;                      // tài khoản đăng nhập SMTP
    std::string smtpPasswordFile;                  // đường dẫn file chứa mật khẩu SMTP (1 dòng, không hardcode)
    std::string smtpFromAddress;                   // địa chỉ email người gửi (vd: noreply@truong.edu.vn)
    std::string smtpFromName       = "Hub32";      // tên hiển thị người gửi
    int         smtpTimeoutSec     = 10;           // timeout kết nối tới SMTP server (giây)
    bool        smtpVerifySsl      = true;         // xác thực certificate SMTP (luôn true trong production)

    // URL gốc ứng dụng — dùng tạo link xác thực trong email
    std::string appBaseUrl;       // vd: "https://app.truong.edu.vn" hoặc "http://localhost:11081"
                                  // Rỗng = tự suy từ bindAddress + port

    // Môi trường — kiểm soát hành vi debug
    std::string environment        = "development"; // "development" | "production"
                                                    // production: ẩn debug_token trong response đăng ký

    // Load from JSON file or Windows Registry; returns error on invalid config
    HUB32API_EXPORT static Result<ServerConfig> from_file(const std::string& path);
    HUB32API_EXPORT static Result<ServerConfig> from_registry();  // Windows Registry
    HUB32API_EXPORT static ServerConfig defaults();
};

} // namespace hub32api
