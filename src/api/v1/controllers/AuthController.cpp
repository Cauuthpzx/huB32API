#include "core/PrecompiledHeader.hpp"   // will adjust relative path after src/ restructure
#include "AuthController.hpp"
#include "../dto/AuthDto.hpp"
#include "../dto/ErrorDto.hpp"
#include "auth/JwtAuth.hpp"
#include "auth/VeyonKeyAuth.hpp"

// cpp-httplib
#include <httplib.h>

namespace veyon32api::api::v1 {

AuthController::AuthController(
    auth::JwtAuth&      jwtAuth,
    auth::VeyonKeyAuth& keyAuth)
    : m_jwtAuth(jwtAuth)
    , m_keyAuth(keyAuth)
{}

void AuthController::handleLogin(const httplib::Request& req, httplib::Response& res)
{
    // TODO: parse dto::AuthRequest from req.body
    // TODO: route to m_keyAuth.authenticate() or logon auth based on method field
    // TODO: on success: m_jwtAuth.issueToken(subject, role)
    // TODO: return dto::AuthResponse as JSON with status 200
    // TODO: on failure: return dto::ErrorDto with status 401
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

void AuthController::handleLogout(const httplib::Request& req, httplib::Response& res)
{
    // TODO: extract jti from Authorization header
    // TODO: m_jwtAuth.revokeToken(jti)
    // TODO: return 204 No Content
    res.status = 501;
    res.set_content(R"({"error":"not implemented"})", "application/json");
}

} // namespace veyon32api::api::v1
