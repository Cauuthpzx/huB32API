#include "PrecompiledHeader.hpp"
#include "hub32api/core/Constants.hpp"

namespace hub32api {

// -- UserRole ---------------------------------------------------------------
std::string to_string(UserRole role)
{
    switch (role) {
        case UserRole::Admin:    return "admin";
        case UserRole::Teacher:  return "teacher";
        case UserRole::Readonly: return "readonly";
        case UserRole::Agent:    return "agent";
    }
    return "readonly";
}

UserRole user_role_from_string(const std::string& s)
{
    if (s == "admin")    return UserRole::Admin;
    if (s == "teacher")  return UserRole::Teacher;
    if (s == "agent")    return UserRole::Agent;
    return UserRole::Readonly;
}

// -- JwtAlgorithm -----------------------------------------------------------
std::string to_string(JwtAlgorithm alg)
{
    switch (alg) {
        case JwtAlgorithm::RS256: return "RS256";
        case JwtAlgorithm::HS256: return "HS256";
    }
    return "RS256";
}

JwtAlgorithm jwt_algorithm_from_string(const std::string& s)
{
    if (s == "HS256") return JwtAlgorithm::HS256;
    return JwtAlgorithm::RS256;
}

// -- AuthMethod -------------------------------------------------------------
std::string to_string(AuthMethod m)
{
    switch (m) {
        case AuthMethod::Logon:    return "logon";
        case AuthMethod::Hub32Key: return "hub32-key";
    }
    return "logon";
}

AuthMethod auth_method_from_string(const std::string& s)
{
    // Support both string names and Veyon-compatible UUIDs
    if (s == "hub32-key" || s == kAuthMethodHub32KeyUuid) return AuthMethod::Hub32Key;
    if (s == "logon"     || s == kAuthMethodLogonUuid)    return AuthMethod::Logon;
    return AuthMethod::Logon;
}

// -- PowerAction ------------------------------------------------------------
std::string to_string(PowerAction a)
{
    switch (a) {
        case PowerAction::Shutdown: return "shutdown";
        case PowerAction::Reboot:   return "reboot";
        case PowerAction::Logoff:   return "logoff";
    }
    return "shutdown";
}

PowerAction power_action_from_string(const std::string& s)
{
    if (s == "reboot")  return PowerAction::Reboot;
    if (s == "logoff")  return PowerAction::Logoff;
    return PowerAction::Shutdown;
}

// -- MediaKind --------------------------------------------------------------
std::string to_string(MediaKind k)
{
    switch (k) {
        case MediaKind::Audio: return "audio";
        case MediaKind::Video: return "video";
    }
    return "video";
}

MediaKind media_kind_from_string(const std::string& s)
{
    if (s == "audio") return MediaKind::Audio;
    return MediaKind::Video;
}

} // namespace hub32api
