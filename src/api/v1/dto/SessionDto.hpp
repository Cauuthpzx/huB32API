#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "veyon32api/plugins/SessionPluginInterface.hpp"

namespace veyon32api::api::v1::dto {

struct UserDto
{
    std::string login;
    std::string fullName;
    std::string domain;

    static UserDto from(const UserInfo& u);
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UserDto, login, fullName, domain)

struct SessionDto
{
    int         sessionId    = -1;
    std::string userLogin;
    std::string userFullName;
    std::string clientAddress;
    int64_t     uptimeSeconds = 0;
    std::string sessionType;

    static SessionDto from(const SessionInfo& s);
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SessionDto,
    sessionId, userLogin, userFullName, clientAddress, uptimeSeconds, sessionType)

struct ScreenDto
{
    int x=0, y=0, width=0, height=0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ScreenDto, x, y, width, height)

} // namespace veyon32api::api::v1::dto
