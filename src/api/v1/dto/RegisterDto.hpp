#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace hub32api::api::v1::dto {

struct RegisterRequest {
    std::string orgName;
    std::string email;
    std::string password;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RegisterRequest, orgName, email, password)

struct RegisterResponse {
    std::string message;
    std::string debugToken;  // only populated when not in production mode
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RegisterResponse, message, debugToken)

} // namespace hub32api::api::v1::dto
