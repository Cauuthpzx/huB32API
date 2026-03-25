/**
 * @file Hub32KeyAuth.cpp
 * @brief Implementation of Hub32KeyAuth — bridges Hub32's key-based authentication.
 *
 * SECURITY: Authentication key hash is loaded from a file at startup and stored
 * in memory. Verification uses PBKDF2-SHA256 with constant-time comparison
 * (OpenSSL CRYPTO_memcmp) via UserRoleStore::verifyPassword.
 *
 * ATTACK PREVENTED: Previously used std::getenv("HUB32_AUTH_KEY") with
 * non-constant-time string comparison (operator!=). This had two flaws:
 *   (a) Timing side-channel — attacker can measure response time to
 *       brute-force the key one byte at a time
 *   (b) Environment variable exposure — visible in /proc/self/environ,
 *       process listings, container inspection, and crash dumps
 *
 * Blast radius: arbitrary agent impersonation, commands sent to student PCs.
 *
 * @todo Replace with real Hub32Core key auth when linking against Hub32Core.
 *       The real implementation should:
 *       - Load the Hub32 auth key from creds.keyData (PEM-encoded private key)
 *       - Authenticate against the Hub32 server using Hub32Connection + AuthenticationManager
 *       - On success, return the authenticated subject string for JWT issuance
 */

#include "../core/PrecompiledHeader.hpp"
#include "Hub32KeyAuth.hpp"
#include "UserRoleStore.hpp"

namespace hub32api::auth {

/**
 * @brief Constructs Hub32KeyAuth with a pre-loaded key hash.
 * @param keyHash PBKDF2-SHA256 hash string (empty = all auth disabled).
 */
Hub32KeyAuth::Hub32KeyAuth(const std::string& keyHash)
    : m_keyHash(keyHash)
{}

/**
 * @brief Authenticates a user using Hub32 key-based credentials.
 *
 * Validates @c creds.keyData against the stored PBKDF2-SHA256 key hash
 * using constant-time comparison. If no key hash is configured,
 * authentication is unconditionally rejected to prevent unauthenticated access.
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
        return Result<std::string>::fail(ApiError{
            ErrorCode::InvalidCredentials,
            "keyName must not be empty"
        });
    }

    // SECURITY: Key validation using stored hash with constant-time comparison.
    // ATTACK PREVENTED: Previously used std::getenv("HUB32_AUTH_KEY") with
    // non-constant-time string comparison — timing side-channel attack.
    // Now uses PBKDF2 hash verification via CRYPTO_memcmp.
    if (m_keyHash.empty()) {
        spdlog::warn("[Hub32KeyAuth] no auth key configured — rejecting all key auth attempts");
        return Result<std::string>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Hub32 key authentication is not configured"
        });
    }

    if (!UserRoleStore::verifyPassword(creds.keyData, m_keyHash)) {
        spdlog::warn("[Hub32KeyAuth] authentication failed for keyName='{}'", creds.keyName);
        return Result<std::string>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Hub32 key authentication failed"
        });
    }

    spdlog::info("[Hub32KeyAuth] authentication successful for keyName='{}'",
                 creds.keyName);
    return Result<std::string>::ok(creds.keyName);
}

} // namespace hub32api::auth
