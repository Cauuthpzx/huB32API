#pragma once

#include <string>
#include <chrono>
#include "hub32api/export.h"

namespace hub32api {

/**
 * @brief Parsed JWT payload for per-request authentication.
 *
 * Populated by JwtValidator::validate() after successful signature
 * verification and claims extraction.  Carried inside AuthContext for
 * the lifetime of a single HTTP request.
 */
struct HUB32API_EXPORT JwtToken
{
    /** @brief Subject claim ("sub") — typically the authenticated username. */
    std::string subject;

    /** @brief Issuer claim ("iss") — expected to equal "hub32api". */
    std::string issuer;

    /** @brief Custom role claim — one of "admin", "teacher", "readonly", "owner", "student", "superadmin". */
    std::string role;

    /** @brief Tenant ID claim ("tid") — identifies the tenant for multi-tenant deployments. Empty for superadmin. */
    std::string tenant_id;

    /** @brief JWT ID claim ("jti") — unique token identifier used for revocation. */
    std::string jti;

    /** @brief Issued-at claim ("iat") converted to a system_clock time_point. */
    std::chrono::system_clock::time_point issuedAt;

    /** @brief Expiry claim ("exp") converted to a system_clock time_point. */
    std::chrono::system_clock::time_point expiresAt;

    /**
     * @brief Returns true if the token has expired relative to the current wall clock.
     * @return @c true when @c now() > expiresAt.
     */
    bool is_expired() const noexcept {
        return std::chrono::system_clock::now() > expiresAt;
    }

    /**
     * @brief Checks whether this token carries a specific role.
     * @param required The role string to compare against (e.g. "admin").
     * @return @c true when @c role == @p required.
     */
    bool has_role(const std::string& required) const noexcept {
        return role == required;
    }
};

} // namespace hub32api
