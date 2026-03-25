#pragma once

#include <string>
#include <memory>
#include "hub32api/auth/AuthContext.hpp"
#include "hub32api/core/Types.hpp"

namespace hub32api::core::internal {

// -----------------------------------------------------------------------
// ApiContext — per-request context object.
// Binds together: auth token, target computer, connection token, request ID.
// Controllers receive this (not raw HTTP request params) to avoid coupling.
// Mirrors Hub32's per-connection LockingPointer pattern.
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

} // namespace hub32api::core::internal
