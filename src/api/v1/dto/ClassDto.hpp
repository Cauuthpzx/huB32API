#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace hub32api::api::v1::dto {

struct CreateClassRequest {
    std::string schoolId;
    std::string name;
    std::string teacherId;  // empty = no teacher assigned
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CreateClassRequest, schoolId, name, teacherId)

struct UpdateClassRequest {
    std::string name;
    std::string teacherId;  // empty = clear teacher assignment
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UpdateClassRequest, name, teacherId)

struct ClassResponse {
    std::string id;
    std::string tenantId;
    std::string schoolId;
    std::string name;
    std::string teacherId;
    int64_t     createdAt = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClassResponse, id, tenantId, schoolId, name,
                                                teacherId, createdAt)

} // namespace hub32api::api::v1::dto
