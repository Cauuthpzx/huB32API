/**
 * @file Hub32KeyAuth.cpp
 * @brief Implementation of Hub32KeyAuth — bridges Hub32's key-based authentication.
 *
 * This is currently a configurable mock implementation for testing purposes.
 * It accepts any username with the password "hub32" and returns the keyName
 * as the JWT subject.
 *
 * @todo Replace with real Hub32Core key auth when linking against Hub32Core.
 *       The real implementation should:
 *       - Load the Hub32 auth key from creds.keyData (PEM-encoded private key)
 *       - Authenticate against the Hub32 server using Hub32Connection + AuthenticationManager
 *       - On success, return the authenticated subject string for JWT issuance
 */

#include "../core/PrecompiledHeader.hpp"
#include "Hub32KeyAuth.hpp"

namespace hub32api::auth {

namespace {

/// The mock password accepted during testing. Replace with real auth logic.
constexpr const char* k_mockPassword = "hub32";

} // anonymous namespace

/**
 * @brief Authenticates a user using Hub32 key-based credentials.
 *
 * In the current mock implementation, this accepts any keyName when the
 * keyData matches the mock password "hub32". On success, returns the
 * keyName as the subject string for JWT issuance.
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

    // TODO: Replace with real Hub32Core key auth when linking.
    //
    // Real implementation would:
    //   1. Parse creds.keyData as a PEM-encoded private key
    //   2. Create a Hub32Connection to the local Hub32 server
    //   3. Use AuthenticationManager to authenticate with the key
    //   4. On success, extract the authenticated subject/username
    //
    // For now, accept any keyName with the mock password for testing.
    if (creds.keyData == k_mockPassword)
    {
        spdlog::info("[Hub32KeyAuth] authentication successful for keyName='{}'",
                     creds.keyName);
        return Result<std::string>::ok(creds.keyName);
    }

    spdlog::warn("[Hub32KeyAuth] authentication failed for keyName='{}'", creds.keyName);
    return Result<std::string>::fail(ApiError{
        ErrorCode::AuthenticationFailed,
        "Hub32 key authentication failed for '" + creds.keyName + "'"
    });
}

} // namespace hub32api::auth
