#pragma once

#include <mutex>
#include <set>
#include "hub32api/plugins/FeaturePluginInterface.hpp"

namespace hub32api::core::internal { class Hub32CoreWrapper; }
namespace hub32api::agent { class AgentRegistry; }

namespace hub32api::plugins {

/**
 * @brief Plugin that bridges Hub32 FeatureManager to the hub32api feature API.
 *
 * When an AgentRegistry is attached, feature control commands are routed
 * to live agents via the command queue. The agent picks up the command,
 * executes it locally (lock screen, capture screen, etc.), and reports
 * the result back. When no agent is online for the target computer,
 * the plugin falls back to mock/local behavior.
 */
class FeaturePlugin final : public FeaturePluginInterface
{
public:
    /** @brief Constructs the FeaturePlugin with a reference to the core wrapper. */
    explicit FeaturePlugin(core::internal::Hub32CoreWrapper& core);

    HUB32API_PLUGIN_METADATA(
        "a1b2c3d4-0002-0002-0002-000000000002",
        "FeaturePlugin",
        "Bridges Hub32 FeatureManager",
        "1.0.0"
    )

    /** @brief Initializes the FeaturePlugin and logs readiness. */
    bool initialize() override;

    /**
     * @brief Attaches the AgentRegistry for live command routing.
     * @param registry Pointer to AgentRegistry (nullptr to use mock behavior only).
     */
    void setAgentRegistry(agent::AgentRegistry* registry);

    /** @brief Lists all available features, optionally filtered by computer. */
    Result<std::vector<FeatureDescriptor>> listFeatures(const Uid& computerUid) override;

    /** @brief Checks whether a specific feature is currently active on a computer. */
    Result<bool>  isFeatureActive(const Uid& computerUid, const Uid& featureUid) override;

    /** @brief Starts, stops, or initializes a feature on a single computer. */
    Result<void>  controlFeature(const Uid& computerUid, const Uid& featureUid,
                                  FeatureOperation op, const FeatureArgs& args) override;

    /** @brief Applies a feature operation to multiple computers in batch. */
    Result<std::vector<Uid>> controlFeatureBatch(
        const std::vector<Uid>& computerUids, const Uid& featureUid,
        FeatureOperation op, const FeatureArgs& args) override;

private:
    core::internal::Hub32CoreWrapper& m_core;
    agent::AgentRegistry* m_agentRegistry = nullptr; ///< Optional live agent registry

    /// @brief Protects @c m_activeFeatures for thread-safe concurrent access.
    /// Recursive because controlFeatureBatch() calls controlFeature() while holding the lock.
    mutable std::recursive_mutex m_mutex;

    /**
     * @brief Tracks which features are active on each computer.
     *
     * Key: computer UID, Value: set of active feature UIDs.
     */
    std::map<std::string, std::set<std::string>> m_activeFeatures;

    /**
     * @brief Maps feature UIDs used in the API to the agent feature UIDs.
     *
     * e.g., "feat-lock-screen" → "lock-screen" (matching agent handler featureUid).
     */
    static std::string agentFeatureUid(const Uid& featureUid);

    /**
     * @brief Generates a UUID v4 string for command IDs.
     */
    static std::string generateCommandId();
};

} // namespace hub32api::plugins
