#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace hub32api::api::v1::dto {

struct CreateStudentRequest {
    std::string classId;
    std::string fullName;
    std::string username;
    std::string password;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CreateStudentRequest, classId, fullName, username, password)

struct StudentResponse {
    std::string id;
    std::string tenantId;
    std::string classId;
    std::string fullName;
    std::string username;
    bool        isActivated = false;
    std::string machineId;
    int64_t     createdAt   = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StudentResponse, id, tenantId, classId, fullName,
                                                username, isActivated, machineId, createdAt)

struct UpdateStudentRequest {
    std::string fullName;
    std::string password; // empty = do not change password
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UpdateStudentRequest, fullName, password)

struct ActivateMachineRequest {
    std::string machineFingerprint;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ActivateMachineRequest, machineFingerprint)

} // namespace hub32api::api::v1::dto
