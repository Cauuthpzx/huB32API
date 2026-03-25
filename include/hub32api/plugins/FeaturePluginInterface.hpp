#pragma once

#include <vector>
#include <map>
#include "hub32api/plugins/PluginInterface.hpp"
#include "hub32api/core/Types.hpp"
#include "hub32api/core/Result.hpp"

namespace hub32api {

// -----------------------------------------------------------------------
// FeatureDescriptor — metadata for a Hub32 feature exposed via API
// -----------------------------------------------------------------------
struct HUB32API_EXPORT FeatureDescriptor
{
    Uid         uid;
    std::string name;
    std::string description;
    bool        isActive    = false;
    bool        isMasterSide = false;
    bool        isServiceSide = false;
    std::string iconUrl;
};

using FeatureArgs = std::map<std::string, std::string>;

// -----------------------------------------------------------------------
// FeaturePluginInterface — wraps Hub32 FeatureManager / FeatureProviderInterface
// -----------------------------------------------------------------------
class HUB32API_EXPORT FeaturePluginInterface : public PluginInterface
{
public:
    virtual Result<std::vector<FeatureDescriptor>> listFeatures(
        const Uid& computerUid) = 0;

    virtual Result<bool> isFeatureActive(
        const Uid& computerUid, const Uid& featureUid) = 0;

    virtual Result<void> controlFeature(
        const Uid& computerUid,
        const Uid& featureUid,
        FeatureOperation op,
        const FeatureArgs& args = {}) = 0;

    virtual Result<std::vector<Uid>> controlFeatureBatch(
        const std::vector<Uid>& computerUids,
        const Uid& featureUid,
        FeatureOperation op,
        const FeatureArgs& args = {}) = 0;
};

} // namespace hub32api
