#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "hub32api/plugins/SessionPluginInterface.hpp"

namespace hub32api::api::v1::dto {

/**
 * @brief Data Transfer Object representing the currently logged-in user.
 */
struct UserDto
{
    std::string login;
    std::string fullName;
    std::string domain;

    /**
     * @brief Constructs a UserDto from a domain UserInfo value.
     * @param u The user information returned by the session plugin.
     * @return A populated UserDto ready for JSON serialisation.
     */
    static UserDto from(const UserInfo& u)
    {
        return UserDto{u.login, u.fullName, u.domain};
    }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UserDto, login, fullName, domain)

/**
 * @brief Data Transfer Object representing an active remote session on a computer.
 */
struct SessionDto
{
    int         sessionId    = -1;
    std::string userLogin;
    std::string userFullName;
    std::string clientAddress;
    int64_t     uptimeSeconds = 0;
    std::string sessionType;

    /**
     * @brief Constructs a SessionDto from a domain SessionInfo value.
     * @param s The session information returned by the session plugin.
     * @return A populated SessionDto ready for JSON serialisation.
     */
    static SessionDto from(const SessionInfo& s)
    {
        return SessionDto{s.sessionId, s.userLogin, s.userFullName,
                          s.clientAddress, s.uptimeSeconds, s.sessionType};
    }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SessionDto,
    sessionId, userLogin, userFullName, clientAddress, uptimeSeconds, sessionType)

struct ScreenDto
{
    int x=0, y=0, width=0, height=0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ScreenDto, x, y, width, height)

} // namespace hub32api::api::v1::dto
