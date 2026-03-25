#include "core/PrecompiledHeader.hpp"
#include "AuthMiddleware.hpp"
#include "auth/JwtAuth.hpp"
#include "core/internal/ApiContext.hpp"

#include <httplib.h>

namespace veyon32api::api::v1::middleware {

AuthMiddleware::AuthMiddleware(auth::JwtAuth& jwtAuth)
    : m_jwtAuth(jwtAuth) {}

bool AuthMiddleware::process(
    const httplib::Request& req,
    httplib::Response& res,
    core::internal::ApiContext& ctx)
{
    // TODO: extract "Authorization: Bearer <token>" header
    // TODO: call m_jwtAuth.authenticate(token)
    // TODO: on success: populate ctx.auth from result
    // TODO: on failure: set res.status=401, return false
    res.status = 501;
    res.set_content(R"({"error":"auth middleware not implemented"})", "application/json");
    return false;
}

} // namespace veyon32api::api::v1::middleware
