#pragma once

#include <string>
#include "hub32api/auth/JwtToken.hpp"
#include "hub32api/core/Result.hpp"

namespace hub32api::auth::internal {

// -----------------------------------------------------------------------
// JwtValidator — validates JWT signature and claims (RS256 only).
// Uses jwt-cpp library internally.
// -----------------------------------------------------------------------
class JwtValidator
{
public:
    explicit JwtValidator(const std::string& publicKey);

    Result<JwtToken> validate(const std::string& rawToken) const;

private:
    std::string m_publicKey;
};

} // namespace hub32api::auth::internal
