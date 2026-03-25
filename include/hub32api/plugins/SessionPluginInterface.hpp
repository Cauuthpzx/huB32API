#pragma once

#include <string>
#include "hub32api/plugins/PluginInterface.hpp"
#include "hub32api/core/Types.hpp"
#include "hub32api/core/Result.hpp"

namespace hub32api {

// -----------------------------------------------------------------------
// SessionInfo — mirrors Hub32's PlatformSessionFunctions::SessionInfo
// -----------------------------------------------------------------------
struct HUB32API_EXPORT SessionInfo
{
    int         sessionId       = -1;
    std::string userLogin;
    std::string userFullName;
    std::string clientAddress;
    int64_t     uptimeSeconds   = 0;
    std::string sessionType;    // "console" | "rdp" | "ssh"
    std::string sessionClientName;
    std::string sessionHostName;
};

// -----------------------------------------------------------------------
// UserInfo — resolved from Hub32's user group backend
// -----------------------------------------------------------------------
struct HUB32API_EXPORT UserInfo
{
    std::string login;
    std::string fullName;
    std::string domain;
    std::vector<std::string> groups;
};

// -----------------------------------------------------------------------
// SessionPluginInterface — wraps Hub32 session/user introspection
// -----------------------------------------------------------------------
class HUB32API_EXPORT SessionPluginInterface : public PluginInterface
{
public:
    virtual Result<SessionInfo> getSession(const Uid& computerUid) = 0;
    virtual Result<UserInfo>    getUser(const Uid& computerUid) = 0;
    virtual Result<std::vector<ScreenRect>> getScreens(const Uid& computerUid) = 0;
};

} // namespace hub32api
