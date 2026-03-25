#pragma once

#include "hub32api/plugins/ComputerPluginInterface.hpp"

namespace hub32api::core::internal { class Hub32CoreWrapper; }
namespace hub32api::agent { class AgentRegistry; }

namespace hub32api::plugins {

/**
 * @brief Bridges Hub32's ComputerControlInterface and live agent data
 *        to the ComputerPluginInterface API.
 *
 * When an AgentRegistry is attached and has online agents, the computer
 * list is built from live agent registrations. Otherwise, mock data is
 * returned for standalone testing.
 */
class ComputerPlugin final : public ComputerPluginInterface
{
public:
    explicit ComputerPlugin(core::internal::Hub32CoreWrapper& core);
    ~ComputerPlugin() override = default;

    HUB32API_PLUGIN_METADATA(
        "a1b2c3d4-0001-0001-0001-000000000001",
        "ComputerPlugin",
        "Bridges Hub32 ComputerControlInterface",
        "1.0.0"
    )

    bool initialize() override;
    void shutdown()   override;

    /**
     * @brief Attaches the AgentRegistry for live agent data.
     * @param registry Pointer to AgentRegistry (nullptr to use mock data only).
     */
    void setAgentRegistry(agent::AgentRegistry* registry);

    Result<std::vector<ComputerInfo>> listComputers() override;
    Result<ComputerInfo>              getComputer(const Uid& uid) override;
    Result<ComputerState>             getState(const Uid& uid) override;
    Result<FramebufferImage>          getFramebuffer(
        const Uid& uid, int width, int height, ImageFormat fmt,
        int compression = -1, int quality = -1) override;

private:
    core::internal::Hub32CoreWrapper& m_core;
    agent::AgentRegistry* m_agentRegistry = nullptr; ///< Optional live agent registry
};

} // namespace hub32api::plugins
