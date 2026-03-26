#pragma once

#include <string>
#include <stdexcept>
#include "hub32api/export.h"

namespace hub32api {

// -----------------------------------------------------------------------
// Error codes (maps to HTTP status codes in the API layer)
// -----------------------------------------------------------------------
enum class ErrorCode
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

    // Crypto errors
    CryptoFailure                 = 5003,    // Maps to HTTP 500

    // Config errors
    InvalidConfig                 = 5004,    // Maps to HTTP 500

    // File errors
    FileReadError                 = 5005,    // Maps to HTTP 500
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
    if (c >= 4000 && c < 6000) return c / 10;  // packed: 4001→400, 4011→401, 5001→500, 5031→503
    if (c >= 400  && c < 600)  return c;        // exact HTTP codes (400, 401, 404, 500…)
    return 500;
}

/**
 * @brief Returns a human-readable string for the given ErrorCode.
 */
HUB32API_EXPORT std::string to_string(ErrorCode code);

// -----------------------------------------------------------------------
// ApiError — thrown internally, caught by the HTTP layer
// -----------------------------------------------------------------------
struct HUB32API_EXPORT ApiError : std::exception
{
    ErrorCode   code;
    std::string message;

    explicit ApiError(ErrorCode c, std::string msg = {})
        : code(c), message(std::move(msg)) {}

    const char* what() const noexcept override { return message.c_str(); }
};

} // namespace hub32api
