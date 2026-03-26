#include "core/PrecompiledHeader.hpp"
#include "BatchController.hpp"
#include "../dto/BatchDto.hpp"
#include "core/internal/PluginRegistry.hpp"
#include "core/internal/I18n.hpp"
#include "api/common/HttpErrorUtil.hpp"
#include <httplib.h>

using hub32api::api::common::sendError;

namespace {

std::string getLocale(const httplib::Request& req) {
    auto* i = hub32api::core::internal::I18n::instance();
    if (!i) return "en";
    return i->negotiate(req.get_header_value("Accept-Language"));
}

} // anonymous namespace

namespace hub32api::api::v2 {

/**
 * @brief Constructs a BatchController.
 *
 * @param registry  Plugin registry used to obtain the feature plugin.
 */
BatchController::BatchController(core::internal::PluginRegistry& registry)
    : m_registry(registry) {}

/**
 * @brief Handles POST /api/v2/batch/features.
 *
 * Parses a `dto::BatchFeatureRequest` from the JSON request body, validates
 * the fields, maps the operation string to a `FeatureOperation` enum value,
 * and delegates to `FeaturePluginInterface::controlFeatureBatch`.  The result
 * is a `dto::BatchFeatureResponse` containing per-computer success/error
 * information.
 *
 * Error handling:
 * - 400 if the JSON body is malformed or missing required fields.
 * - 400 if `computerIds` is empty, `featureUid` is empty, or `operation` is
 *   not one of `"start"` / `"stop"`.
 * - 503 if the feature plugin is not loaded.
 * - 200 with a `dto::BatchFeatureResponse` otherwise (individual computer
 *   failures are reported inside the response body rather than as HTTP errors).
 *
 * @param req  Incoming HTTP request; the body must be a valid UTF-8 JSON
 *             object conforming to `dto::BatchFeatureRequest`.
 * @param res  Outgoing HTTP response.
 */
void BatchController::handleBatchFeature(
    const httplib::Request& req, httplib::Response& res)
{
    using hub32api::core::internal::tr;
    const auto lang = getLocale(req);

    // -----------------------------------------------------------------------
    // Parse and validate request body
    // -----------------------------------------------------------------------
    dto::BatchFeatureRequest batchReq;
    try {
        const auto j = nlohmann::json::parse(req.body);
        batchReq = j.get<dto::BatchFeatureRequest>();
    } catch (const nlohmann::json::exception& ex) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), std::string(ex.what()));
        return;
    }

    // Validate computerIds
    if (batchReq.computerIds.empty()) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), tr(lang, "error.missing_computer_ids"));
        return;
    }

    // Validate featureUid
    if (batchReq.featureUid.empty()) {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), tr(lang, "error.missing_feature_uid"));
        return;
    }

    // Validate operation string
    FeatureOperation op;
    if (batchReq.operation == "start") {
        op = FeatureOperation::Start;
    } else if (batchReq.operation == "stop") {
        op = FeatureOperation::Stop;
    } else {
        sendError(res, 400, tr(lang, "error.invalid_request_body"), tr(lang, "error.invalid_operation", {batchReq.operation}));
        return;
    }

    // -----------------------------------------------------------------------
    // Obtain the feature plugin
    // -----------------------------------------------------------------------
    auto* featurePlugin = m_registry.featurePlugin();
    if (!featurePlugin) {
        sendError(res, 503, tr(lang, "error.feature_plugin_unavailable"));
        return;
    }

    // -----------------------------------------------------------------------
    // Execute the batch operation
    // controlFeatureBatch returns the UIDs of computers for which the operation
    // succeeded.  Any UID absent from the success list is treated as failed.
    // -----------------------------------------------------------------------
    const FeatureArgs args(batchReq.arguments.begin(), batchReq.arguments.end());
    const auto& computerIds = batchReq.computerIds;

    auto batchResult = featurePlugin->controlFeatureBatch(
        computerIds, batchReq.featureUid, op, args);

    // Build a set of succeeded UIDs for O(1) lookup
    std::unordered_map<std::string, bool> succeededSet;
    std::string batchErrorDetail;

    if (batchResult.is_ok()) {
        for (const auto& uid : batchResult.value()) {
            succeededSet[uid] = true;
        }
    } else {
        // The entire batch call failed — treat every computer as failed
        batchErrorDetail = batchResult.error().message;
    }

    // -----------------------------------------------------------------------
    // Build the response DTO
    // -----------------------------------------------------------------------
    dto::BatchFeatureResponse response;
    response.total = static_cast<int>(computerIds.size());

    for (const auto& cid : computerIds) {
        dto::BatchResultItem item;
        item.computerId = cid;

        if (!batchErrorDetail.empty()) {
            item.success = false;
            item.error   = batchErrorDetail;
        } else {
            item.success = (succeededSet.count(cid) > 0);
            if (!item.success) {
                item.error = "Operation did not complete successfully";
            }
        }

        if (item.success) {
            ++response.succeeded;
        } else {
            ++response.failed;
        }

        response.results.push_back(std::move(item));
    }

    res.status = 200;
    res.set_content(nlohmann::json(response).dump(), "application/json");
}

} // namespace hub32api::api::v2
