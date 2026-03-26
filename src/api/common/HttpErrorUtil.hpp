#pragma once
#include <string>

namespace httplib { class Response; }

namespace hub32api::api::common {

/**
 * @brief Sends an RFC-7807-style JSON error response.
 * @param res    The httplib response to populate.
 * @param status HTTP status code.
 * @param title  Short human-readable problem title.
 * @param detail Longer explanation; defaults to title when empty.
 */
void sendError(httplib::Response& res, int status,
               const std::string& title, const std::string& detail = {});

} // namespace hub32api::api::common
