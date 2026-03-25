#pragma once

#include <gmock/gmock.h>
#include "veyon32api/plugins/FeaturePluginInterface.hpp"

namespace veyon32api {

class MockFeaturePlugin : public FeaturePluginInterface
{
public:
    VEYON32API_PLUGIN_METADATA(
        "a1b2c3d4-0002-0002-0002-000000000002",
        "MockFeaturePlugin", "Test mock", "0.0.1"
    )

    MOCK_METHOD(Result<std::vector<FeatureDescriptor>>, listFeatures, (const Uid&), (override));
    MOCK_METHOD(Result<bool>, isFeatureActive, (const Uid&, const Uid&), (override));
    MOCK_METHOD(Result<void>, controlFeature,
                (const Uid&, const Uid&, FeatureOperation, const FeatureArgs&), (override));
    MOCK_METHOD(Result<std::vector<Uid>>, controlFeatureBatch,
                (const std::vector<Uid>&, const Uid&, FeatureOperation, const FeatureArgs&), (override));
};

} // namespace veyon32api
