#pragma once

#include <string>
#include "hub32api/export.h"
#include "hub32api/core/Result.hpp"

namespace hub32api::auth {

// -----------------------------------------------------------------------
// Hub32KeyAuth — bridges Hub32's key-based authentication method.
// Authenticates with a Hub32 auth key file, then issues a JWT.
// -----------------------------------------------------------------------
class HUB32API_EXPORT Hub32KeyAuth
{
public:
    /**
     * @brief Constructs Hub32KeyAuth with a pre-loaded key hash.
     * @param keyHash PBKDF2-SHA256 hash of the auth key (empty = auth disabled).
     *
     * SECURITY: The key hash is loaded from a file at startup rather than
     * from an environment variable. This prevents exposure via /proc/self/environ,
     * crash dumps, and container inspection.
     */
    explicit Hub32KeyAuth(const std::string& keyHash = {});

    struct Credentials {
        std::string keyName;
        std::string keyData; // PEM-encoded private key
    };

    Result<std::string> authenticate(const Credentials& creds) const;

private:
    std::string m_keyHash;  ///< PBKDF2-SHA256 hash of the auth key
};

} // namespace hub32api::auth
