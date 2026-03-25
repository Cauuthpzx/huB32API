#pragma once

#include <optional>
#include "hub32api/auth/JwtToken.hpp"
#include "hub32api/export.h"

namespace hub32api {

/**
 * @brief Per-request authentication context bound to every HTTP request.
 *
 * Populated by AuthMiddleware after successful JWT validation.
 * Controllers receive this via ApiContext and use the role-check helpers
 * to enforce access control without touching raw JWT claims directly.
 */
struct HUB32API_EXPORT AuthContext
{
    /** @brief True when the request carries a valid, non-expired, non-revoked JWT. */
    bool authenticated = false;

    /** @brief The validated token, or std::nullopt for anonymous requests. */
    std::optional<JwtToken> token;

    /**
     * @brief Returns true if the authenticated user holds the "admin" role.
     * @return @c true when @c token is present and @c token->role == "admin".
     */
    bool is_admin() const noexcept { return token && token->has_role("admin"); }

    /**
     * @brief Returns true if the authenticated user holds the "teacher" role.
     * @return @c true when @c token is present and @c token->role == "teacher".
     */
    bool is_teacher() const noexcept { return token && token->has_role("teacher"); }

    /**
     * @brief Returns true if the authenticated user holds the "readonly" role.
     * @return @c true when @c token is present and @c token->role == "readonly".
     */
    bool is_readonly() const noexcept { return token && token->has_role("readonly"); }

    /**
     * @brief Returns the subject (username) of the authenticated user.
     *
     * Returns a reference to a static empty string when the context is
     * anonymous (no token present), so callers never receive a dangling ref.
     *
     * @return Reference to @c token->subject, or a static empty string.
     */
    const std::string& subject() const noexcept {
        static std::string empty;
        return token ? token->subject : empty;
    }

    /**
     * @brief Factory method that constructs an unauthenticated AuthContext.
     * @return A default-constructed AuthContext with @c authenticated == false.
     */
    static AuthContext anonymous() noexcept { return AuthContext{}; }
};

} // namespace hub32api
