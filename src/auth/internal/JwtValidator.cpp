#include "../../core/PrecompiledHeader.hpp"
#include "JwtValidator.hpp"

namespace veyon32api::auth::internal {

JwtValidator::JwtValidator(const std::string& secret)
    : m_secret(secret)
{}

Result<JwtToken> JwtValidator::validate(const std::string& rawToken) const
{
    // TODO: use jwt-cpp to verify HS256 signature, expiry, and extract claims
    spdlog::debug("[JwtValidator] validate stub");
    return Result<JwtToken>::fail(ApiError{
        ErrorCode::NotImplemented, "JwtValidator::validate not yet implemented"
    });
}

} // namespace veyon32api::auth::internal
