/**
 * @file Hub32KeyAuth.cpp
 * @brief Implementation of Hub32KeyAuth — bridges Hub32's key-based authentication.
 *
 * Authentication key is read from the @c HUB32_AUTH_KEY environment variable.
 * If the variable is not set, all key-auth attempts are rejected and a
 * warning is logged on every attempt.
 *
 * @todo Replace with real Hub32Core key auth when linking against Hub32Core.
 *       The real implementation should:
 *       - Load the Hub32 auth key from creds.keyData (PEM-encoded private key)
 *       - Authenticate against the Hub32 server using Hub32Connection + AuthenticationManager
 *       - On success, return the authenticated subject string for JWT issuance
 */

#include "../core/PrecompiledHeader.hpp"
#include "Hub32KeyAuth.hpp"

#include <cstdlib>

namespace hub32api::auth {

/**
 * @brief Authenticates a user using Hub32 key-based credentials.
 *
 * Validates @c creds.keyData against the @c HUB32_AUTH_KEY environment
 * variable.  If the environment variable is not set, authentication is
 * unconditionally rejected to prevent unauthenticated access.
 *
 * @todo Replace with real Hub32Core key auth when linking against Hub32Core.
 *
 * @param creds The credentials containing keyName and keyData (PEM key).
 * @return Result containing the subject string on success, or an ApiError
 *         with AuthenticationFailed on invalid credentials.
 */
Result<std::string> Hub32KeyAuth::authenticate(const Credentials& creds) const
{
    spdlog::info("[Hub32KeyAuth] authentication attempt for keyName='{}'", creds.keyName);

    if (creds.keyName.empty())
    {
        spdlog::warn("[Hub32KeyAuth] authentication failed: empty keyName");
        return Result<std::string>::fail(ApiError{
            ErrorCode::InvalidCredentials,
            "keyName must not be empty"
        });
    }

    // Get auth key from environment (secure: not hardcoded).
    // If the environment variable is absent, reject all key-auth attempts.
    const char* envKey = std::getenv("HUB32_AUTH_KEY");
    if (!envKey) {
        spdlog::warn("[Hub32KeyAuth] HUB32_AUTH_KEY not set — rejecting all key auth attempts");
        return Result<std::string>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Hub32 key authentication is not configured (HUB32_AUTH_KEY not set)"
        });
    }

    if (creds.keyData != envKey)
    {
        spdlog::warn("[Hub32KeyAuth] authentication failed for keyName='{}'", creds.keyName);
        return Result<std::string>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Hub32 key authentication failed for '" + creds.keyName + "'"
        });
    }

    spdlog::info("[Hub32KeyAuth] authentication successful for keyName='{}'",
                 creds.keyName);
    return Result<std::string>::ok(creds.keyName);
}

} // namespace hub32api::auth
