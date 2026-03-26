#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace hub32api::api::v1::dto {

struct CreateSchoolRequest {
    std::string name;
    std::string address;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CreateSchoolRequest, name, address)

struct SchoolResponse {
    std::string id;
    std::string name;
    std::string address;
    int64_t     createdAt = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SchoolResponse, id, name, address, createdAt)

struct CreateLocationRequest {
    std::string schoolId;
    std::string name;
    std::string building;
    int         floor = 0;
    int         capacity = 0;
    std::string type = "classroom";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CreateLocationRequest, schoolId, name, building, floor, capacity, type)

struct LocationResponse {
    std::string id;
    std::string schoolId;
    std::string name;
    std::string building;
    int         floor = 0;
    int         capacity = 0;
    std::string type;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocationResponse, id, schoolId, name, building, floor, capacity, type)

} // namespace hub32api::api::v1::dto
