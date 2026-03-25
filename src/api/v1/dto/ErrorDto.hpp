#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "hub32api/core/Error.hpp"

namespace hub32api::api::v1::dto {

/**
 * @brief RFC 7807 "Problem Details" compatible error response DTO.
 *
 * Provides two factory overloads: one from an @ref ApiError domain value
 * and one from a raw HTTP status + message string for quick inline errors.
 */
struct ErrorDto
{
    int         status  = 500;
    std::string type;       ///< URI reference, e.g. "/errors/404"
    std::string title;      ///< Short human-readable summary
    std::string detail;     ///< Longer explanation
    std::string instance;   ///< Request path that caused the error

    /**
     * @brief Constructs an ErrorDto from a domain ApiError.
     * @param e    The error value carrying an ErrorCode and message.
     * @param path Optional request path for the `instance` field.
     * @return A populated ErrorDto.
     */
    static ErrorDto from(const ApiError& e, const std::string& path = {})
    {
        const int status = http_status_for(e.code);
        return ErrorDto{
            status,
            "/errors/" + std::to_string(static_cast<int>(e.code)),
            e.message,
            e.message,
            path
        };
    }

    /**
     * @brief Constructs an ErrorDto directly from an HTTP status code and message.
     * @param httpStatus  The HTTP status to report (e.g. 400, 404, 503).
     * @param msg         Human-readable title/detail string.
     * @param path        Optional request path for the `instance` field.
     * @return A populated ErrorDto.
     */
    static ErrorDto from(int httpStatus, const std::string& msg, const std::string& path = {})
    {
        return ErrorDto{
            httpStatus,
            "/errors/" + std::to_string(httpStatus),
            msg,
            msg,
            path
        };
    }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ErrorDto, status, type, title, detail, instance)

} // namespace hub32api::api::v1::dto
