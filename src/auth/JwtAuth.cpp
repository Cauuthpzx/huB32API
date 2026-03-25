#include "../core/PrecompiledHeader.hpp"
#include "JwtAuth.hpp"
#include "internal/JwtValidator.hpp"
#include "internal/TokenStore.hpp"

namespace veyon32api::auth {

struct JwtAuth::Impl
{
    std::string secret;
    int         expirySeconds;
    std::unique_ptr<internal::JwtValidator> validator;
    std::unique_ptr<internal::TokenStore>   store;
};

JwtAuth::JwtAuth(const ServerConfig& cfg)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->secret        = cfg.jwtSecret;
    m_impl->expirySeconds = cfg.jwtExpirySeconds;
    m_impl->validator     = std::make_unique<internal::JwtValidator>(cfg.jwtSecret);
    m_impl->store         = std::make_unique<internal::TokenStore>();
}

JwtAuth::~JwtAuth() = default;

Result<std::string> JwtAuth::issueToken(
    const std::string& subject,
    const std::string& role) const
{
    // TODO: use jwt-cpp to create signed HS256 token with exp, iat, sub, role claims
    spdlog::debug("[JwtAuth] issueToken stub for subject={}", subject);
    return Result<std::string>::fail(ApiError{
        ErrorCode::NotImplemented, "JwtAuth::issueToken not yet implemented"
    });
}

Result<AuthContext> JwtAuth::authenticate(const std::string& bearerToken) const
{
    // TODO: strip "Bearer " prefix, call m_impl->validator->validate(), check denylist
    spdlog::debug("[JwtAuth] authenticate stub");
    return Result<AuthContext>::fail(ApiError{
        ErrorCode::NotImplemented, "JwtAuth::authenticate not yet implemented"
    });
}

void JwtAuth::revokeToken(const std::string& jti)
{
    m_impl->store->revoke(jti);
}

} // namespace veyon32api::auth
