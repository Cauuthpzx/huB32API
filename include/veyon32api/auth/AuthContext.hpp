#pragma once

#include <optional>
#include "veyon32api/auth/JwtToken.hpp"
#include "veyon32api/export.h"

namespace veyon32api {

// -----------------------------------------------------------------------
// AuthContext — bound to each HTTP request after JWT validation.
// Controllers receive this via dependency injection / middleware.
// -----------------------------------------------------------------------
struct VEYON32API_EXPORT AuthContext
{
    bool               authenticated = false;
    std::optional<JwtToken> token;

    bool is_admin()    const noexcept;
    bool is_teacher()  const noexcept;
    bool is_readonly() const noexcept;
    const std::string& subject() const noexcept;

    static AuthContext anonymous() noexcept;
};

} // namespace veyon32api
