#pragma once

#include <string>
#include "veyon32api/plugins/PluginInterface.hpp"
#include "veyon32api/core/Types.hpp"
#include "veyon32api/core/Result.hpp"

namespace veyon32api {

// -----------------------------------------------------------------------
// SessionInfo — mirrors Veyon's PlatformSessionFunctions::SessionInfo
// -----------------------------------------------------------------------
struct VEYON32API_EXPORT SessionInfo
{
    int         sessionId       = -1;
    std::string userLogin;
    std::string userFullName;
    std::string clientAddress;
    int64_t     uptimeSeconds   = 0;
    std::string sessionType;    // "console" | "rdp" | "ssh"
};

// -----------------------------------------------------------------------
// UserInfo — resolved from Veyon's user group backend
// -----------------------------------------------------------------------
struct VEYON32API_EXPORT UserInfo
{
    std::string login;
    std::string fullName;
    std::string domain;
    std::vector<std::string> groups;
};

// -----------------------------------------------------------------------
// SessionPluginInterface — wraps Veyon session/user introspection
// -----------------------------------------------------------------------
class VEYON32API_EXPORT SessionPluginInterface : public PluginInterface
{
public:
    virtual Result<SessionInfo> getSession(const Uid& computerUid) = 0;
    virtual Result<UserInfo>    getUser(const Uid& computerUid) = 0;
    virtual Result<std::vector<ScreenRect>> getScreens(const Uid& computerUid) = 0;
};

} // namespace veyon32api
