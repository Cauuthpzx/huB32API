#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "veyon32api/core/Error.hpp"

namespace veyon32api::api::v1::dto {

// -----------------------------------------------------------------------
// ErrorDto — RFC 7807 "Problem Details" compatible error response.
// -----------------------------------------------------------------------
struct ErrorDto
{
    int         status  = 500;
    std::string type;       // URI reference, e.g. "/errors/unauthorized"
    std::string title;      // Short human-readable summary
    std::string detail;     // Longer explanation
    std::string instance;   // Request path that caused the error

    static ErrorDto from(const ApiError& e, const std::string& path = {});
    static ErrorDto from(int httpStatus, const std::string& msg, const std::string& path = {});
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ErrorDto, status, type, title, detail, instance)

} // namespace veyon32api::api::v1::dto
