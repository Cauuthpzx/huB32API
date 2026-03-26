#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace hub32api::api::v1::dto {

struct CreateTeacherRequest {
    std::string username;
    std::string password;
    std::string fullName;
    std::string role = "teacher";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CreateTeacherRequest, username, password, fullName, role)

struct TeacherResponse {
    std::string id;
    std::string username;
    std::string fullName;
    std::string role;
    int64_t     createdAt = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TeacherResponse, id, username, fullName, role, createdAt)

struct AssignLocationRequest {
    std::string locationId;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AssignLocationRequest, locationId)

} // namespace hub32api::api::v1::dto
