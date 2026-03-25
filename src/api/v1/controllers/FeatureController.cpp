#include "core/PrecompiledHeader.hpp"
#include "FeatureController.hpp"
#include "../dto/FeatureDto.hpp"
#include "../dto/ErrorDto.hpp"
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

namespace veyon32api::api::v1 {

/**
 * @brief Constructs the FeatureController.
 * @param registry The plugin registry used to resolve the feature plugin.
 */
FeatureController::FeatureController(core::internal::PluginRegistry& registry)
    : m_registry(registry)
{}

/**
 * @brief Handles GET /api/v1/computers/:id/features — lists all features for a computer.
 *
 * The computer UID is extracted from the first regex capture group
 * (@c req.matches[1]).  Each @ref FeatureDescriptor is mapped to a
 * @ref dto::FeatureDto and returned as a @ref dto::FeatureListDto JSON body.
 *
 * Returns HTTP 200 on success, HTTP 503 if the feature plugin is unavailable
 * or if the underlying call fails.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void FeatureController::handleList(const httplib::Request& req, httplib::Response& res)
{
    const std::string computerId = req.matches[1].str();

    auto* plugin = m_registry.featurePlugin();
    if (!plugin) {
        sendError(res, 503, "Feature plugin unavailable");
        return;
    }

    const auto result = plugin->listFeatures(computerId);
    if (result.is_err()) {
        sendError(res, 503, "Failed to list features", result.error().message);
        return;
    }

    dto::FeatureListDto listDto;
    for (const auto& fd : result.value()) {
        listDto.features.push_back(dto::FeatureDto::from(fd));
    }

    const nlohmann::json j = listDto;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles GET /api/v1/computers/:id/features/:fid — returns a single feature.
 *
 * The computer UID is extracted from @c req.matches[1] and the feature UID
 * from @c req.matches[2].  The active state is fetched separately via
 * @ref FeaturePluginInterface::isFeatureActive and merged into the returned DTO.
 *
 * Returns HTTP 200 with a @ref dto::FeatureDto body, HTTP 404 if the feature
 * UID is not found in the feature list, or HTTP 503 on plugin error.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void FeatureController::handleGetOne(const httplib::Request& req, httplib::Response& res)
{
    const std::string computerId = req.matches[1].str();
    const std::string featureUid = req.matches[2].str();

    auto* plugin = m_registry.featurePlugin();
    if (!plugin) {
        sendError(res, 503, "Feature plugin unavailable");
        return;
    }

    // Retrieve the full feature list to find the descriptor
    const auto listResult = plugin->listFeatures(computerId);
    if (listResult.is_err()) {
        sendError(res, 503, "Failed to list features", listResult.error().message);
        return;
    }

    // Find the descriptor matching featureUid
    const auto& features = listResult.value();
    const auto it = std::find_if(features.begin(), features.end(),
                                 [&](const FeatureDescriptor& fd) {
                                     return fd.uid == featureUid;
                                 });

    if (it == features.end()) {
        sendError(res, 404, "Feature not found",
                  "No feature with uid: " + featureUid);
        return;
    }

    // Build DTO; override isActive with the live query
    auto dto = dto::FeatureDto::from(*it);
    const auto activeResult = plugin->isFeatureActive(computerId, featureUid);
    if (activeResult.is_ok()) {
        dto.isActive = activeResult.value();
    }

    const nlohmann::json j = dto;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

/**
 * @brief Handles PUT /api/v1/computers/:id/features/:fid — starts or stops a feature.
 *
 * The computer UID is extracted from @c req.matches[1] and the feature UID
 * from @c req.matches[2].  The request body must be a JSON object conforming to
 * @ref dto::FeatureControlRequest:
 * @code
 * { "active": true, "arguments": { "key": "value" } }
 * @endcode
 *
 * @c active=true  maps to @ref FeatureOperation::Start.
 * @c active=false maps to @ref FeatureOperation::Stop.
 *
 * Returns HTTP 200 @c {"success":true} on success, HTTP 400 on parse error,
 * or HTTP 503 on plugin error.
 *
 * @param req  The incoming HTTP request.
 * @param res  The HTTP response to populate.
 */
void FeatureController::handleControl(const httplib::Request& req, httplib::Response& res)
{
    const std::string computerId = req.matches[1].str();
    const std::string featureUid = req.matches[2].str();

    // --- Parse request body ---
    dto::FeatureControlRequest ctrl;
    try {
        const auto j = nlohmann::json::parse(req.body);
        ctrl = j.get<dto::FeatureControlRequest>();
    }
    catch (const std::exception& ex) {
        sendError(res, 400, "Invalid request body", ex.what());
        return;
    }

    auto* plugin = m_registry.featurePlugin();
    if (!plugin) {
        sendError(res, 503, "Feature plugin unavailable");
        return;
    }

    // Map active flag to FeatureOperation
    const FeatureOperation op = ctrl.active
        ? FeatureOperation::Start
        : FeatureOperation::Stop;

    // The DTO arguments map is already std::map<string,string> == FeatureArgs
    const FeatureArgs args(ctrl.arguments.begin(), ctrl.arguments.end());

    const auto result = plugin->controlFeature(computerId, featureUid, op, args);
    if (result.is_err()) {
        const auto& err = result.error();
        const int status = http_status_for(err.code);
        sendError(res, status, "Feature control failed", err.message);
        return;
    }

    nlohmann::json j;
    j["success"] = true;
    res.status = 200;
    res.set_content(j.dump(), "application/json");
}

} // namespace veyon32api::api::v1
