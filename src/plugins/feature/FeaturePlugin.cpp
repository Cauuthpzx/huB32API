#include "core/PrecompiledHeader.hpp"
#include "FeaturePlugin.hpp"
#include "core/internal/Hub32CoreWrapper.hpp"

namespace hub32api::plugins {

/**
 * @brief Constructs the FeaturePlugin with a reference to the core wrapper.
 * @param core Reference to the Hub32CoreWrapper used for future Hub32Core integration.
 */
FeaturePlugin::FeaturePlugin(core::internal::Hub32CoreWrapper& core)
    : m_core(core) {}

/**
 * @brief Initializes the FeaturePlugin.
 *
 * When Hub32Core is linked, this will cache features from
 * Hub32Core::featureManager().features() for quick lookup.
 *
 * @return true if initialization succeeded.
 */
bool FeaturePlugin::initialize()
{
    spdlog::info("[FeaturePlugin] initialized");
    return true;
}

/**
 * @brief Lists all available features for the given computer.
 *
 * Returns a set of mock FeatureDescriptor entries representing typical
 * Veyon classroom-management features. The isActive flag reflects the
 * current in-memory state tracked by controlFeature().
 *
 * @param computerUid The computer to list features for (used for per-computer active state).
 * @return Result containing a vector of FeatureDescriptor on success.
 */
Result<std::vector<FeatureDescriptor>> FeaturePlugin::listFeatures(const Uid& computerUid)
{
    // Build the static feature catalog
    std::vector<FeatureDescriptor> features = {
        FeatureDescriptor{
            "feat-lock-screen",
            "Lock Screen",
            "Lock computer screen",
            false,   // isActive (overridden below)
            true,    // isMasterSide
            true,    // isServiceSide
            ""       // iconUrl
        },
        FeatureDescriptor{
            "feat-screen-broadcast",
            "Screen Broadcast",
            "Broadcast teacher screen",
            false,
            true,
            true,
            ""
        },
        FeatureDescriptor{
            "feat-input-lock",
            "Input Lock",
            "Lock keyboard and mouse",
            false,
            false,
            true,
            ""
        },
        FeatureDescriptor{
            "feat-message",
            "Show Message",
            "Display message on screen",
            false,
            true,
            true,
            ""
        },
        FeatureDescriptor{
            "feat-power-control",
            "Power Control",
            "Power on/off/reboot",
            false,
            true,
            false,
            ""
        },
    };

    // Reflect per-computer active state from m_activeFeatures
    auto it = m_activeFeatures.find(computerUid);
    if (it != m_activeFeatures.end())
    {
        const auto& activeSet = it->second;
        for (auto& feat : features)
        {
            if (activeSet.count(feat.uid) > 0)
            {
                feat.isActive = true;
            }
        }
    }

    return Result<std::vector<FeatureDescriptor>>::ok(std::move(features));
}

/**
 * @brief Checks whether a specific feature is currently active on a computer.
 *
 * Looks up the computer's active feature set in the in-memory tracking map.
 *
 * @param computerUid The computer to query.
 * @param featureUid  The feature to check.
 * @return Result containing true if the feature is active, false otherwise.
 */
Result<bool> FeaturePlugin::isFeatureActive(const Uid& computerUid, const Uid& featureUid)
{
    auto compIt = m_activeFeatures.find(computerUid);
    if (compIt == m_activeFeatures.end())
    {
        return Result<bool>::ok(false);
    }

    bool active = compIt->second.count(featureUid) > 0;
    return Result<bool>::ok(active);
}

/**
 * @brief Starts, stops, or initializes a feature on a single computer.
 *
 * - FeatureOperation::Start: adds the feature to the computer's active set.
 * - FeatureOperation::Stop: removes the feature from the computer's active set.
 * - FeatureOperation::Initialize: no-op (logged only).
 *
 * When Hub32Core is linked, this will delegate to
 * Hub32Core::featureManager().controlFeature().
 *
 * @param computerUid The target computer.
 * @param featureUid  The feature to control.
 * @param op          The operation to perform (Start, Stop, Initialize).
 * @param args        Optional key-value arguments for the feature operation.
 * @return Result<void> indicating success.
 */
Result<void> FeaturePlugin::controlFeature(
    const Uid& computerUid, const Uid& featureUid,
    FeatureOperation op, const FeatureArgs& args)
{
    switch (op)
    {
    case FeatureOperation::Start:
        m_activeFeatures[computerUid].insert(featureUid);
        spdlog::info("[FeaturePlugin] Started feature '{}' on computer '{}'",
                     featureUid, computerUid);
        break;

    case FeatureOperation::Stop:
        {
            auto it = m_activeFeatures.find(computerUid);
            if (it != m_activeFeatures.end())
            {
                it->second.erase(featureUid);
                if (it->second.empty())
                {
                    m_activeFeatures.erase(it);
                }
            }
            spdlog::info("[FeaturePlugin] Stopped feature '{}' on computer '{}'",
                         featureUid, computerUid);
        }
        break;

    case FeatureOperation::Initialize:
        spdlog::info("[FeaturePlugin] Initialize feature '{}' on computer '{}'",
                     featureUid, computerUid);
        break;
    }

    return Result<void>::ok();
}

/**
 * @brief Applies a feature operation to multiple computers in batch.
 *
 * Iterates over the provided computer UIDs and calls controlFeature() for
 * each one. Returns the list of UIDs for which the operation succeeded.
 * With mock data, all operations succeed.
 *
 * @param computerUids The list of target computers.
 * @param featureUid   The feature to control.
 * @param op           The operation to perform (Start, Stop, Initialize).
 * @param args         Optional key-value arguments for the feature operation.
 * @return Result containing a vector of UIDs that succeeded.
 */
Result<std::vector<Uid>> FeaturePlugin::controlFeatureBatch(
    const std::vector<Uid>& computerUids, const Uid& featureUid,
    FeatureOperation op, const FeatureArgs& args)
{
    std::vector<Uid> succeeded;
    succeeded.reserve(computerUids.size());

    for (const auto& compUid : computerUids)
    {
        auto result = controlFeature(compUid, featureUid, op, args);
        if (result.is_ok())
        {
            succeeded.push_back(compUid);
        }
    }

    spdlog::info("[FeaturePlugin] Batch {} feature '{}' on {}/{} computers",
                 to_string(op), featureUid, succeeded.size(), computerUids.size());

    return Result<std::vector<Uid>>::ok(std::move(succeeded));
}

} // namespace hub32api::plugins
