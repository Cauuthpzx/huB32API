#pragma once

#include "hub32api/plugins/FeaturePluginInterface.hpp"

namespace hub32api::core::internal { class Hub32CoreWrapper; }

namespace hub32api::plugins {

class FeaturePlugin final : public FeaturePluginInterface
{
public:
    explicit FeaturePlugin(core::internal::Hub32CoreWrapper& core);

    HUB32API_PLUGIN_METADATA(
        "a1b2c3d4-0002-0002-0002-000000000002",
        "FeaturePlugin",
        "Bridges Hub32 FeatureManager",
        "1.0.0"
    )

    bool initialize() override;

    Result<std::vector<FeatureDescriptor>> listFeatures(const Uid& computerUid) override;
    Result<bool>  isFeatureActive(const Uid& computerUid, const Uid& featureUid) override;
    Result<void>  controlFeature(const Uid& computerUid, const Uid& featureUid,
                                  FeatureOperation op, const FeatureArgs& args) override;
    Result<std::vector<Uid>> controlFeatureBatch(
        const std::vector<Uid>& computerUids, const Uid& featureUid,
        FeatureOperation op, const FeatureArgs& args) override;

private:
    core::internal::Hub32CoreWrapper& m_core;
};

} // namespace hub32api::plugins
