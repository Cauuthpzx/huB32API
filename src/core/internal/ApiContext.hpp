#pragma once

#include <string>
#include <memory>
#include "veyon32api/auth/AuthContext.hpp"
#include "veyon32api/core/Types.hpp"

namespace veyon32api::core::internal {

// -----------------------------------------------------------------------
// ApiContext — per-request context object.
// Binds together: auth token, target computer, connection token, request ID.
// Controllers receive this (not raw HTTP request params) to avoid coupling.
// Mirrors Veyon's per-connection LockingPointer pattern.
// -----------------------------------------------------------------------
struct ApiContext
{
    std::string  requestId;        // X-Request-ID or generated UUID
    AuthContext  auth;             // Validated JWT or anonymous
    Uid          connectionToken;  // Active pool connection token
    std::string  targetHostname;   // Resolved from computer UID
    Port         targetPort = 0;

    bool isAuthenticated() const noexcept { return auth.authenticated; }
};

} // namespace veyon32api::core::internal
