#pragma once

#include "hub32api/plugins/ComputerPluginInterface.hpp"

namespace hub32api::core::internal { class Hub32CoreWrapper; }

namespace hub32api::plugins {

// -----------------------------------------------------------------------
// ComputerPlugin — bridges Hub32's ComputerControlInterface and
// NetworkObjectDirectoryManager to the ComputerPluginInterface API.
// -----------------------------------------------------------------------
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

    Result<std::vector<ComputerInfo>> listComputers() override;
    Result<ComputerInfo>              getComputer(const Uid& uid) override;
    Result<ComputerState>             getState(const Uid& uid) override;
    Result<FramebufferImage>          getFramebuffer(
        const Uid& uid, int width, int height, ImageFormat fmt) override;

private:
    core::internal::Hub32CoreWrapper& m_core;
};

} // namespace hub32api::plugins
