#pragma once

#include <string>
#include <stdexcept>
#include "veyon32api/export.h"

namespace veyon32api {

// -----------------------------------------------------------------------
// Error codes (maps to HTTP status codes in the API layer)
// -----------------------------------------------------------------------
enum class VEYON32API_EXPORT ErrorCode
{
    None                          = 0,
    // 400
    InvalidRequest                = 400,
    InvalidCredentials            = 4001,
    InvalidFeature                = 4002,
    InvalidConnection             = 4003,
    AuthMethodNotAvailable        = 4004,
    // 401
    Unauthorized                  = 401,
    AuthenticationFailed          = 4011,
    TokenExpired                  = 4012,
    // 404
    NotFound                      = 404,
    ComputerNotFound              = 4041,
    // 408
    RequestTimeout                = 408,
    ConnectionTimeout             = 4081,
    // 429
    TooManyRequests               = 429,
    ConnectionLimitReached        = 4291,
    // 500
    InternalError                 = 500,
    FramebufferEncodingError      = 5001,
    PluginError                   = 5002,
    // 501
    NotImplemented                = 501,
    ProtocolMismatch              = 5011,
    // 503
    ServiceUnavailable            = 503,
    FramebufferNotAvailable       = 5031,
};

/**
 * @brief Maps an internal ErrorCode to an HTTP status integer.
 *
 * Codes in the range [4000, 5000) are "packed" codes of the form
 * HTTP_STATUS * 10 + sub-index, e.g. 4041 -> 404.
 * Codes already in the standard HTTP range [400, 600) are returned as-is.
 * Any other code maps to 500 (Internal Server Error).
 */
inline int http_status_for(ErrorCode code)
{
    int c = static_cast<int>(code);
    if (c >= 4000 && c < 5000) return c / 10;  // e.g. 4001→400, 4011→401, 4041→404
    if (c >= 400  && c < 600)  return c;        // exact HTTP codes (400, 401, 404, 500…)
    return 500;
}

// -----------------------------------------------------------------------
// ApiError — thrown internally, caught by the HTTP layer
// -----------------------------------------------------------------------
struct VEYON32API_EXPORT ApiError : std::exception
{
    ErrorCode   code;
    std::string message;

    explicit ApiError(ErrorCode c, std::string msg = {})
        : code(c), message(std::move(msg)) {}

    const char* what() const noexcept override { return message.c_str(); }
};

} // namespace veyon32api
