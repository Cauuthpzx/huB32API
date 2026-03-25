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
 * Performs the following checks:
 *  - httpPort is within the valid TCP port range (1-65535).
 *  - metricsPort is within the valid TCP port range (1-65535) when metrics
 *    are enabled.
 *  - jwtSecret is not empty.
 *  - jwtExpirySeconds is positive.
 *  - workerThreads is at least 1.
 *  - connectionLimitPerHost is at least 1.
 *  - globalConnectionLimit is at least connectionLimitPerHost.
 *  - When TLS is enabled, tlsCertFile and tlsKeyFile are not empty.
 *
 * @param cfg  The ServerConfig to validate.
 * @return A vector of error strings.  Empty if the configuration is valid.
 */
std::vector<std::string> ConfigValidator::validate(const ServerConfig& cfg) const
{
    std::vector<std::string> errors;

    // --- Port range checks ---
    if (cfg.httpPort == 0) {
        errors.emplace_back("httpPort must be in the range 1-65535");
    }

    if (cfg.metricsEnabled && cfg.metricsPort == 0) {
        errors.emplace_back("metricsPort must be in the range 1-65535 when metrics are enabled");
    }

    // --- JWT ---
    if (cfg.jwtSecret.empty()) {
        errors.emplace_back("jwtSecret must not be empty");
    }

    if (cfg.jwtExpirySeconds <= 0) {
        errors.emplace_back("jwtExpirySeconds must be positive");
    }

    // --- Threads ---
    if (cfg.workerThreads < 1) {
        errors.emplace_back("workerThreads must be at least 1");
    }

    // --- Connection limits ---
    if (cfg.connectionLimitPerHost < 1) {
        errors.emplace_back("connectionLimitPerHost must be at least 1");
    }

    if (cfg.globalConnectionLimit < cfg.connectionLimitPerHost) {
        errors.emplace_back("globalConnectionLimit must be >= connectionLimitPerHost");
    }

    // --- TLS ---
    if (cfg.tlsEnabled) {
        if (cfg.tlsCertFile.empty()) {
            errors.emplace_back("tlsCertFile must not be empty when TLS is enabled");
        }
        if (cfg.tlsKeyFile.empty()) {
            errors.emplace_back("tlsKeyFile must not be empty when TLS is enabled");
        }
    }

    return errors;
}

} // namespace hub32api::config::internal
