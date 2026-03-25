#pragma once

#include <string>
#include "hub32api/core/Result.hpp"

namespace hub32api::auth {

// -----------------------------------------------------------------------
// Hub32KeyAuth — bridges Hub32's key-based authentication method.
// Authenticates with a Hub32 auth key file, then issues a JWT.
// -----------------------------------------------------------------------
class Hub32KeyAuth
{
public:
    Hub32KeyAuth() = default;

    struct Credentials {
        std::string keyName;
        std::string keyData; // PEM-encoded private key
    };

    Result<std::string> authenticate(const Credentials& creds) const;
};

} // namespace hub32api::auth
