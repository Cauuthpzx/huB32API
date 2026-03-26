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
        const bool isJson =
            ct.find("application/json") != std::string::npos;
        const bool isOctet =
            ct.find("application/octet-stream") != std::string::npos;
        if (!isJson && !isOctet) {
            nlohmann::json body;
            body["status"] = 415;
            body["title"]  = tr(lang, "error.unsupported_media_type");
            body["detail"] = tr(lang, "error.unsupported_media_type");
            res.status = 415;
            res.set_content(body.dump(), "application/json");
            return false;
        }

        // -------------------------------------------------------------------
        // 3. Deep JSON value validation (field length, array size, depth)
        // -------------------------------------------------------------------
        if (isJson) {
            nlohmann::json parsed;
            try {
                parsed = nlohmann::json::parse(req.body);
            } catch (const nlohmann::json::parse_error&) {
                nlohmann::json body;
                body["status"] = 400;
                body["title"]  = tr(lang, "error.bad_request");
                body["detail"] = tr(lang, "error.invalid_json");
                res.status = 400;
                res.set_content(body.dump(), "application/json");
                return false;
            }

            std::string violation;
            if (!validateJsonValue(parsed, 0, violation)) {
                nlohmann::json body;
                body["status"] = 422;
                body["title"]  = tr(lang, "error.unprocessable_entity");
                body["detail"] = violation;
                res.status = 422;
                res.set_content(body.dump(), "application/json");
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Recursively validates a parsed JSON value against the configured limits.
 *
 * Checks:
 *  - String values do not exceed @c m_cfg.maxFieldLength.
 *  - Object keys do not exceed @c m_cfg.maxFieldLength.
 *  - Arrays do not exceed @c m_cfg.maxArraySize elements.
 *  - Nesting depth does not exceed @c m_cfg.maxPathDepth.
 *
 * @param j          The JSON value to validate.
 * @param depth      Current nesting depth (0 at the root).
 * @param violation  Populated with a human-readable reason on failure.
 * @return @c true if the value passes all checks; @c false otherwise.
 */
bool InputValidationMiddleware::validateJsonValue(const nlohmann::json& j,
                                                  int depth,
                                                  std::string& violation) const
{
    if (depth >= m_cfg.maxPathDepth) {
        violation = "JSON nesting depth exceeds maximum of "
                    + std::to_string(m_cfg.maxPathDepth);
        return false;
    }

    if (j.is_string()) {
        if (j.get<std::string>().size() > m_cfg.maxFieldLength) {
            violation = "String value exceeds maximum length of "
                        + std::to_string(m_cfg.maxFieldLength);
            return false;
        }
    } else if (j.is_array()) {
        if (j.size() > m_cfg.maxArraySize) {
            violation = "Array exceeds maximum size of "
                        + std::to_string(m_cfg.maxArraySize);
            return false;
        }
        for (const auto& elem : j) {
            if (!validateJsonValue(elem, depth + 1, violation))
                return false;
        }
    } else if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key().size() > m_cfg.maxFieldLength) {
                violation = "Object key '" + it.key()
                            + "' exceeds maximum length of "
                            + std::to_string(m_cfg.maxFieldLength);
                return false;
            }
            if (!validateJsonValue(it.value(), depth + 1, violation))
                return false;
        }
    }

    return true;
}

} // namespace hub32api::api::v1::middleware
