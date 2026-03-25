#include "core/PrecompiledHeader.hpp"
#include "FeaturePlugin.hpp"
#include "core/internal/Hub32CoreWrapper.hpp"

namespace hub32api::plugins {

FeaturePlugin::FeaturePlugin(core::internal::Hub32CoreWrapper& core)
    : m_core(core) {}

bool FeaturePlugin::initialize()
{
    // TODO: Cache Hub32Core::featureManager().features() for quick lookup
    spdlog::info("[FeaturePlugin] initialized (stub)");
    return true;
}

Result<std::vector<FeatureDescriptor>> FeaturePlugin::listFeatures(const Uid& /*computerUid*/)
{
    // TODO: Hub32Core::featureManager().features() → map to FeatureDescriptor
    return Result<std::vector<FeatureDescriptor>>::fail(ApiError{
        ErrorCode::NotImplemented, "FeaturePlugin::listFeatures"
    });
}

Result<bool> FeaturePlugin::isFeatureActive(const Uid& /*computerUid*/, const Uid& /*featureUid*/)
{
    // TODO: Check feature state via ComputerControlInterface
    return Result<bool>::ok(false);
}

Result<void> FeaturePlugin::controlFeature(
    const Uid& /*computerUid*/, const Uid& /*featureUid*/,
    FeatureOperation /*op*/, const FeatureArgs& /*args*/)
{
    // TODO: Hub32Core::featureManager().controlFeature(...)
    return Result<void>::fail(ApiError{ErrorCode::NotImplemented, "controlFeature"});
}

Result<std::vector<Uid>> FeaturePlugin::controlFeatureBatch(
    const std::vector<Uid>& computerUids, const Uid& /*featureUid*/,
    FeatureOperation /*op*/, const FeatureArgs& /*args*/)
{
    // TODO: iterate computerUids, call controlFeature for each, collect successes
    return Result<std::vector<Uid>>::ok({});
}

} // namespace hub32api::plugins
