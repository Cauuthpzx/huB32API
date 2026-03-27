#include "PrecompiledHeader.hpp"
#include "hub32api/core/Constants.hpp"

namespace hub32api {

// -- UserRole ---------------------------------------------------------------
std::string to_string(UserRole role)
{
    switch (role) {
        case UserRole::Admin:       return "admin";
        case UserRole::Teacher:     return "teacher";
        case UserRole::Readonly:    return "readonly";
        case UserRole::Agent:       return "agent";
        case UserRole::Superadmin:  return "superadmin";
        case UserRole::Owner:       return "owner";
        case UserRole::Student:     return "student";
    }
    return "readonly";
}

UserRole user_role_from_string(const std::string& s)
{
    if (s == "admin")       return UserRole::Admin;
    if (s == "teacher")     return UserRole::Teacher;
    if (s == "agent")       return UserRole::Agent;
    if (s == "superadmin")  return UserRole::Superadmin;
    if (s == "owner")       return UserRole::Owner;
    if (s == "student")     return UserRole::Student;
    return UserRole::Readonly;
}

// -- JwtAlgorithm -----------------------------------------------------------
std::string to_string(JwtAlgorithm alg)
{
    switch (alg) {
        case JwtAlgorithm::RS256: return "RS256";
    }
    return "RS256";
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

// -- ErrorCode --------------------------------------------------------------
std::string to_string(ErrorCode code)
{
    switch (code) {
        case ErrorCode::None:                    return "None";
        case ErrorCode::InvalidRequest:          return "InvalidRequest";
        case ErrorCode::InvalidCredentials:      return "InvalidCredentials";
        case ErrorCode::InvalidFeature:          return "InvalidFeature";
        case ErrorCode::InvalidConnection:       return "InvalidConnection";
        case ErrorCode::AuthMethodNotAvailable:  return "AuthMethodNotAvailable";
        case ErrorCode::Unauthorized:            return "Unauthorized";
        case ErrorCode::AuthenticationFailed:    return "AuthenticationFailed";
        case ErrorCode::TokenExpired:            return "TokenExpired";
        case ErrorCode::NotFound:                return "NotFound";
        case ErrorCode::ComputerNotFound:        return "ComputerNotFound";
        case ErrorCode::RequestTimeout:          return "RequestTimeout";
        case ErrorCode::ConnectionTimeout:       return "ConnectionTimeout";
        case ErrorCode::Conflict:                return "Conflict";
        case ErrorCode::DuplicateResource:       return "DuplicateResource";
        case ErrorCode::TooManyRequests:         return "TooManyRequests";
        case ErrorCode::ConnectionLimitReached:  return "ConnectionLimitReached";
        case ErrorCode::InternalError:           return "InternalError";
        case ErrorCode::FramebufferEncodingError: return "FramebufferEncodingError";
        case ErrorCode::PluginError:             return "PluginError";
        case ErrorCode::CryptoFailure:           return "CryptoFailure";
        case ErrorCode::InvalidConfig:           return "InvalidConfig";
        case ErrorCode::FileReadError:           return "FileReadError";
        case ErrorCode::PayloadTooLarge:         return "PayloadTooLarge";
        case ErrorCode::UnsupportedMediaType:    return "UnsupportedMediaType";
        case ErrorCode::UnprocessableEntity:     return "UnprocessableEntity";
        case ErrorCode::ValidationFailed:        return "ValidationFailed";
        case ErrorCode::NotImplemented:          return "NotImplemented";
        case ErrorCode::ProtocolMismatch:        return "ProtocolMismatch";
        case ErrorCode::ServiceUnavailable:      return "ServiceUnavailable";
        case ErrorCode::FramebufferNotAvailable: return "FramebufferNotAvailable";
    }
    return "Unknown(" + std::to_string(static_cast<int>(code)) + ")";
}

} // namespace hub32api
