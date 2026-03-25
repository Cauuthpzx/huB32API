#pragma once

#include <string>
#include "veyon32api/core/Result.hpp"

namespace veyon32api::auth {

// -----------------------------------------------------------------------
// VeyonKeyAuth — bridges Veyon's key-based authentication method.
// Authenticates with a Veyon auth key file, then issues a JWT.
// -----------------------------------------------------------------------
class VeyonKeyAuth
{
public:
    VeyonKeyAuth() = default;

    struct Credentials {
        std::string keyName;
        std::string keyData; // PEM-encoded private key
    };

    Result<std::string> authenticate(const Credentials& creds) const;
};

} // namespace veyon32api::auth
