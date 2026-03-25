#pragma once

#include <memory>
#include "veyon32api/core/Result.hpp"

// Forward declarations
namespace httplib { class Request; class Response; }
namespace veyon32api::auth { class JwtAuth; class VeyonKeyAuth; }

namespace veyon32api::api::v1 {

// -----------------------------------------------------------------------
// AuthController — handles POST /api/v1/auth and DELETE /api/v1/auth (logout)
// -----------------------------------------------------------------------
class AuthController
{
public:
    explicit AuthController(
        auth::JwtAuth&      jwtAuth,
        auth::VeyonKeyAuth& keyAuth);

    // POST /api/v1/auth
    void handleLogin(const httplib::Request& req, httplib::Response& res);

    // DELETE /api/v1/auth
    void handleLogout(const httplib::Request& req, httplib::Response& res);

private:
    auth::JwtAuth&      m_jwtAuth;
    auth::VeyonKeyAuth& m_keyAuth;
};

} // namespace veyon32api::api::v1
