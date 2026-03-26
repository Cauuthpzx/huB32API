#include "core/PrecompiledHeader.hpp"
#include "InputValidationMiddleware.hpp"
#include "core/internal/I18n.hpp"
#include <httplib.h>

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

} // anonymous namespace

namespace hub32api::api::v1::middleware {

/**
 * @brief Constructs an InputValidationMiddleware with the given configuration.
 *
 * @param cfg  Validation policy (max body size, field length, array size, path depth).
 */
InputValidationMiddleware::InputValidationMiddleware(ValidationConfig cfg) : m_cfg(cfg) {}

/**
 * @brief Validates basic input constraints before passing the request downstream.
 *
 * Checks performed:
 *  1. Body size — rejects requests exceeding @c maxBodySize with HTTP 413.
 *  2. Content-Type — for POST/PUT/PATCH requests that carry a body, the
 *     Content-Type must contain "application/json" or "application/octet-stream";
 *     otherwise the method returns HTTP 415.
 *
 * @param req  Incoming HTTP request.
 * @param res  Outgoing HTTP response (status and body set on rejection).
 * @return @c true if the request passes all checks; @c false if rejected.
 */
bool InputValidationMiddleware::process(const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // -----------------------------------------------------------------------
    // 1. Body size check
    // -----------------------------------------------------------------------
    if (req.body.size() > m_cfg.maxBodySize) {
        nlohmann::json body;
        body["status"] = 413;
        body["title"]  = tr(lang, "error.payload_too_large");
        body["detail"] = tr(lang, "error.payload_too_large");
        res.status = 413;
        res.set_content(body.dump(), "application/json");
        return false;
    }

    // -----------------------------------------------------------------------
    // 2. Content-Type check for methods that carry a body
    // -----------------------------------------------------------------------
    const std::string& method = req.method;
    if ((method == "POST" || method == "PUT" || method == "PATCH") && !req.body.empty()) {
        const std::string ct = req.get_header_value("Content-Type");
        const bool valid =
            ct.find("application/json") != std::string::npos ||
            ct.find("application/octet-stream") != std::string::npos;
        if (!valid) {
            nlohmann::json body;
            body["status"] = 415;
            body["title"]  = tr(lang, "error.unsupported_media_type");
            body["detail"] = tr(lang, "error.unsupported_media_type");
            res.status = 415;
            res.set_content(body.dump(), "application/json");
            return false;
        }
    }

    return true;
}

} // namespace hub32api::api::v1::middleware
