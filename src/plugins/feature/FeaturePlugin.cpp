#include "core/PrecompiledHeader.hpp"
#include "FeaturePlugin.hpp"
#include "core/internal/Hub32CoreWrapper.hpp"
#include "core/internal/CryptoUtils.hpp"
#include "agent/AgentRegistry.hpp"
#include "hub32api/agent/AgentInfo.hpp"
#include "hub32api/agent/AgentCommand.hpp"

namespace hub32api::plugins {

/**
 * @brief Constructs the FeaturePlugin with a reference to the core wrapper.
 * @param core Reference to the Hub32CoreWrapper used for future Hub32Core integration.
 */
FeaturePlugin::FeaturePlugin(core::internal::Hub32CoreWrapper& core)
    : m_core(core) {}

/**
 * @brief Initializes the FeaturePlugin.
 * @return true if initialization succeeded.
 */
bool FeaturePlugin::initialize()
{
    spdlog::info("[FeaturePlugin] initialized (live agent support: {})",
                 m_agentRegistry ? "yes" : "no");
    return true;
}

/**
 * @brief Attaches the AgentRegistry for live command routing.
 * @param registry Pointer to AgentRegistry (nullptr reverts to mock-only mode).
 */
void FeaturePlugin::setAgentRegistry(agent::AgentRegistry* registry)
{
    m_agentRegistry = registry;
    spdlog::info("[FeaturePlugin] agent registry {}",
                 registry ? "attached" : "detached");
}

/**
 * @brief Maps API feature UIDs to agent handler feature UIDs.
 *
 * The API uses "feat-lock-screen" style UIDs, while agent handlers
 * use "lock-screen" style UIDs.
 *
 * @param featureUid The API-level feature UID.
 * @return The agent-level feature UID.
 */
std::string FeaturePlugin::agentFeatureUid(const Uid& featureUid)
{
    if (featureUid == kFeatureLockScreen)      return std::string(kHandlerLockScreen);
    if (featureUid == kFeatureScreenBroadcast) return std::string(kHandlerScreenCapture);
    if (featureUid == kFeatureInputLock)       return std::string(kHandlerInputLock);
    if (featureUid == kFeatureMessage)         return std::string(kHandlerMessageDisplay);
    if (featureUid == kFeaturePowerControl)    return std::string(kHandlerPowerControl);
    return featureUid; // pass through if already in agent format
}

/**
 * @brief Lists all available features for the given computer.
 *
 * Returns feature descriptors with isActive reflecting the current state.
 *
 * @param computerUid The computer to list features for.
 * @return Result containing a vector of FeatureDescriptor on success.
 */
Result<std::vector<FeatureDescriptor>> FeaturePlugin::listFeatures(const Uid& computerUid)
{
    std::lock_guard lock(m_mutex);
    // Build the static feature catalog
    std::vector<FeatureDescriptor> features = {
        FeatureDescriptor{
            std::string(kFeatureLockScreen),
            "",                // parentUid (top-level)
            "Lock Screen",
            "Lock computer screen",
            false,   // isActive (overridden below)
            true,    // isMasterSide
            true,    // isServiceSide
            ""       // iconUrl
        },
        FeatureDescriptor{
            std::string(kFeatureScreenBroadcast),
            "",                // parentUid (top-level)
            "Screen Broadcast",
            "Broadcast teacher screen",
            false,
            true,
            true,
            ""
        },
        FeatureDescriptor{
            std::string(kFeatureInputLock),
            std::string(kFeatureLockScreen), // parentUid (sub-feature of lock)
            "Input Lock",
            "Lock keyboard and mouse",
            false,
            false,
            true,
            ""
        },
        FeatureDescriptor{
            std::string(kFeatureMessage),
            "",                // parentUid (top-level)
            "Show Message",
            "Display message on screen",
            false,
            true,
            true,
            ""
        },
        FeatureDescriptor{
            std::string(kFeaturePowerControl),
            "",                // parentUid (top-level)
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
    std::lock_guard lock(m_mutex);
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
 * If an AgentRegistry is attached and the target computer corresponds to
 * an online agent, the command is queued for the agent to execute. Otherwise
 * the feature state is tracked locally (mock behavior).
 *
 * @param computerUid The target computer (may be an agent ID).
 * @param featureUid  The feature to control.
 * @param op          The operation to perform (Start, Stop, Initialize).
 * @param args        Optional key-value arguments for the feature operation.
 * @return Result<void> indicating success.
 */
Result<void> FeaturePlugin::controlFeature(
    const Uid& computerUid, const Uid& featureUid,
    FeatureOperation op, const FeatureArgs& args)
{
    std::lock_guard lock(m_mutex);

    // Try routing to a live agent
    if (m_agentRegistry && op != FeatureOperation::Initialize) {
        auto agentResult = m_agentRegistry->findAgent(computerUid);
        if (agentResult.is_ok()) {
            const auto& agent = agentResult.value();
            if (agent.state == AgentState::Online || agent.state == AgentState::Busy) {
                // Build and queue a command for the agent
                AgentCommand cmd;
                auto cmdIdResult = core::internal::CryptoUtils::generateUuid();
                if (cmdIdResult.is_err()) {
                    spdlog::error("[FeaturePlugin] UUID generation failed: {}", cmdIdResult.error().message);
                    return Result<void>::fail(ApiError{ErrorCode::InternalError, "UUID generation failed"});
                }
                cmd.commandId  = cmdIdResult.take();
                cmd.agentId    = computerUid;
                cmd.featureUid = agentFeatureUid(featureUid);
                cmd.operation  = (op == FeatureOperation::Start) ? "start" : "stop";
                cmd.arguments  = args;

                m_agentRegistry->queueCommand(cmd);

                spdlog::info("[FeaturePlugin] Queued '{}' {} on agent '{}' (cmd={})",
                             featureUid, to_string(op), computerUid, cmd.commandId);

                // Update local tracking
                if (op == FeatureOperation::Start) {
                    m_activeFeatures[computerUid].insert(featureUid);
                } else {
                    auto it = m_activeFeatures.find(computerUid);
                    if (it != m_activeFeatures.end()) {
                        it->second.erase(featureUid);
                        if (it->second.empty()) m_activeFeatures.erase(it);
                    }
                }
                return Result<void>::ok();
            }
        }
    }

    // Fall back to mock/local behavior
    switch (op)
    {
    case FeatureOperation::Start:
        m_activeFeatures[computerUid].insert(featureUid);
        spdlog::info("[FeaturePlugin] Started feature '{}' on computer '{}' (mock)",
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
            spdlog::info("[FeaturePlugin] Stopped feature '{}' on computer '{}' (mock)",
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
 * each one. For agents, commands are queued individually. Returns the list
 * of UIDs for which the operation succeeded.
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
    std::lock_guard lock(m_mutex);
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
