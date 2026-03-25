#include "core/PrecompiledHeader.hpp"
#include "FramebufferController.hpp"
#include "core/internal/PluginRegistry.hpp"

#include <httplib.h>

namespace {

/**
 * @brief Sends an RFC-7807-style JSON error response.
 * @param res    The httplib response to populate.
 * @param status HTTP status code to set.
 * @param title  Short human-readable problem title.
 * @param detail Longer explanation; defaults to @p title when empty.
 */
void sendError(httplib::Response& res,
               int                status,
               const std::string& title,
               const std::string& detail = {})
{
    nlohmann::json j;
    j["status"] = status;
    j["title"]  = title;
    j["detail"] = detail.empty() ? title : detail;
    res.status  = status;
    res.set_content(j.dump(), "application/json");
}

} // anonymous namespace

namespace hub32api::api::v1 {

/**
 * @brief Constructs the FramebufferController.
 * @param registry The plugin registry used to resolve the computer plugin.
 */
FramebufferController::FramebufferController(core::internal::PluginRegistry& registry)
    : m_registry(registry)
{}

/**
 * @brief Handles GET /api/v1/computers/:id/framebuffer — captures the remote screen.
 *
 * The computer UID is extracted from the first regex capture group
 * (@c req.matches[1]).
 *
 * Supported query parameters:
 *   - @c width   — desired output width in pixels (0 = native, default 0).
 *   - @c height  — desired output height in pixels (0 = native, default 0).
 *   - @c format  — @c "png" (default) or @c "jpeg".
 *
 * On success the response body contains raw image bytes with the appropriate
 * @c Content-Type, plus @c Cache-Control: no-cache and an @c ETag derived from
 * the image content hash.
 *
 * Returns HTTP 404 if the computer is not found, HTTP 503 if the plugin is
 * unavailable or if framebuffer capture fails.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void FramebufferController::handleGetFramebuffer(
    const httplib::Request& req, httplib::Response& res)
{
    const std::string id = req.matches[1].str();

    auto* plugin = m_registry.computerPlugin();
    if (!plugin) {
        sendError(res, 503, "Computer plugin unavailable");
        return;
    }

    // --- Parse query parameters ---
    int width  = 0;
    int height = 0;
    ImageFormat format = ImageFormat::Png;

    const std::string widthParam  = req.get_param_value("width");
    const std::string heightParam = req.get_param_value("height");
    const std::string formatParam = req.get_param_value("format");

    if (!widthParam.empty()) {
        try { width = std::stoi(widthParam); } catch (...) { width = 0; }
    }
    if (!heightParam.empty()) {
        try { height = std::stoi(heightParam); } catch (...) { height = 0; }
    }
    if (!formatParam.empty()) {
        if (formatParam == "jpeg" || formatParam == "jpg") {
            format = ImageFormat::Jpeg;
        }
        // default is already Png
    }

    // --- Capture framebuffer ---
    const auto result = plugin->getFramebuffer(id, width, height, format);
    if (result.is_err()) {
        const auto& err = result.error();
        const bool notFound = (err.code == ErrorCode::ComputerNotFound ||
                               err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, "Computer not found",
                      "No computer with id: " + id);
        } else {
            const int status = http_status_for(err.code);
            sendError(res, status, "Framebuffer capture failed", err.message);
        }
        return;
    }

    const FramebufferImage& img = result.value();

    // --- Build response ---
    res.body = std::string(img.data.begin(), img.data.end());

    const std::string contentType = (img.format == ImageFormat::Jpeg)
        ? "image/jpeg"
        : "image/png";

    res.set_header("Content-Type",  contentType);
    res.set_header("Cache-Control", "no-cache");
    res.set_header("ETag",
        std::to_string(std::hash<std::string>{}(res.body)));

    res.status = 200;
}

} // namespace hub32api::api::v1
