#pragma once

#include <functional>

namespace httplib { class Request; class Response; }
namespace veyon32api::auth { class JwtAuth; }
namespace veyon32api::core::internal { struct ApiContext; }

namespace veyon32api::api::v1::middleware {

// -----------------------------------------------------------------------
// AuthMiddleware — validates Bearer token before controller dispatch.
// Rejects unauthenticated requests with 401.
// -----------------------------------------------------------------------
class AuthMiddleware
{
public:
    explicit AuthMiddleware(auth::JwtAuth& jwtAuth);

    // Returns true if request should proceed; false if rejected (res already set)
    bool process(const httplib::Request& req, httplib::Response& res,
                 core::internal::ApiContext& ctx);

private:
    auth::JwtAuth& m_jwtAuth;
};

} // namespace veyon32api::api::v1::middleware
