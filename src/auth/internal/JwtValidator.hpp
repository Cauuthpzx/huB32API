#pragma once

#include <string>
#include "veyon32api/auth/JwtToken.hpp"
#include "veyon32api/core/Result.hpp"

namespace veyon32api::auth::internal {

// -----------------------------------------------------------------------
// JwtValidator — validates JWT signature and claims.
// Uses jwt-cpp library internally.
// -----------------------------------------------------------------------
class JwtValidator
{
public:
    explicit JwtValidator(const std::string& secret);

    Result<JwtToken> validate(const std::string& rawToken) const;

private:
    std::string m_secret;
};

} // namespace veyon32api::auth::internal
