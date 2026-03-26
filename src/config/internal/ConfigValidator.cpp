/**
 * @file ConfigValidator.cpp
 * @brief Implementation of ConfigValidator — validates ServerConfig fields.
 *
 * Checks port ranges, non-empty JWT secret, positive thread counts,
 * and sensible connection limits.  Returns a list of human-readable
 * error messages; an empty list means the configuration is valid.
 */

#include "../../core/PrecompiledHeader.hpp"
#include "ConfigValidator.hpp"

namespace hub32api::config::internal {

/**
 * @brief Validates the given ServerConfig and returns any error messages.
 *
 * CRITICAL FIELDS (throw on error):
 *  - httpPort must be in the valid TCP port range (1-65535).
 *  - bindAddress must not be empty.
 *  - jwtAlgorithm must be "RS256".
 *
 * NON-CRITICAL FIELDS (warn on error with sensible defaults):
 *  - metricsPort is within the valid TCP port range (1-65535) when metrics
 *    are enabled.
 *  - jwtSecret is not empty.
 *  - jwtExpirySeconds is positive.
 *  - workerThreads is at least 1.
 *  - connectionLimitPerHost is at least 1.
 *  - globalConnectionLimit is at least connectionLimitPerHost.
 *  - When TLS is enabled, tlsCertFile and tlsKeyFile must not be empty (CRITICAL).
 *
 * @param cfg  The ServerConfig to validate.
 * @return Result with vector of warning strings on success, or ApiError on critical failure.
 */
Result<std::vector<std::string>> ConfigValidator::validate(const ServerConfig& cfg) const
{
    std::vector<std::string> errors;

    // --- CRITICAL: Port range checks ---
    if (cfg.httpPort == 0) {
        return Result<std::vector<std::string>>::fail(ApiError{
            ErrorCode::InvalidConfig,
            "httpPort must be in the range 1-65535 (got " + std::to_string(cfg.httpPort) + ")"
        });
    }

    // --- CRITICAL: Bind address must not be empty ---
    if (cfg.bindAddress.empty()) {
        return Result<std::vector<std::string>>::fail(ApiError{
            ErrorCode::InvalidConfig, "bindAddress must not be empty"
        });
    }

    // --- CRITICAL: JWT algorithm must be RS256 ---
    if (cfg.jwtAlgorithm != to_string(JwtAlgorithm::RS256)) {
        return Result<std::vector<std::string>>::fail(ApiError{
            ErrorCode::InvalidConfig,
            "jwtAlgorithm must be \"RS256\" (got \"" + cfg.jwtAlgorithm + "\"). HS256 is no longer supported."
        });
    }

    // --- NON-CRITICAL: Metrics port (when enabled) ---
    if (cfg.metricsEnabled && cfg.metricsPort == 0) {
        errors.emplace_back("metricsPort must be in the range 1-65535 when metrics are enabled");
    }

    // --- NON-CRITICAL: JWT expiry ---
    if (cfg.jwtExpirySeconds <= 0) {
        errors.emplace_back("jwtExpirySeconds must be positive");
    }

    // --- NON-CRITICAL: Threads ---
    if (cfg.workerThreads < 1) {
        errors.emplace_back("workerThreads must be at least 1");
    }

    // --- NON-CRITICAL: Connection limits ---
    if (cfg.connectionLimitPerHost < 1) {
        errors.emplace_back("connectionLimitPerHost must be at least 1");
    }

    if (cfg.globalConnectionLimit < cfg.connectionLimitPerHost) {
        errors.emplace_back("globalConnectionLimit must be >= connectionLimitPerHost");
    }

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

    return Result<std::vector<std::string>>::ok(std::move(errors));
}

} // namespace hub32api::config::internal
