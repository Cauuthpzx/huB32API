#include "core/PrecompiledHeader.hpp"
#include "FramebufferController.hpp"
#include "core/internal/PluginRegistry.hpp"
#include "core/internal/I18n.hpp"
#include "api/common/HttpErrorUtil.hpp"

#include <httplib.h>
#include <algorithm>

using hub32api::api::common::sendError;

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
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
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    if (req.matches.size() <= 1) {
        sendError(res, 400, tr(lang, "error.missing_path_param"));
        return;
    }
    const std::string id = req.matches[1].str();

    auto* plugin = m_registry.computerPlugin();
    if (!plugin) {
        sendError(res, 503, tr(lang, "error.computer_plugin_unavailable"));
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

    int compression = -1;
    int quality     = -1;
    const std::string compParam = req.get_param_value("compression");
    const std::string qualParam = req.get_param_value("quality");
    if (!compParam.empty()) {
        try { compression = std::clamp(std::stoi(compParam), 0, 9); } catch (...) {}
    }
    if (!qualParam.empty()) {
        try { quality = std::clamp(std::stoi(qualParam), 0, 100); } catch (...) {}
    }

    // --- Capture framebuffer ---
    const auto result = plugin->getFramebuffer(id, width, height, format, compression, quality);
    if (result.is_err()) {
        const auto& err = result.error();
        const bool notFound = (err.code == ErrorCode::ComputerNotFound ||
                               err.code == ErrorCode::NotFound);
        if (notFound) {
            sendError(res, 404, tr(lang, "error.computer_not_found"),
                      tr(lang, "error.no_computer_with_id", {id}));
        } else {
            const int status = http_status_for(err.code);
            sendError(res, status, tr(lang, "error.framebuffer_capture_failed"), err.message);
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
