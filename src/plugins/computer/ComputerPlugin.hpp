#pragma once

#include "veyon32api/plugins/ComputerPluginInterface.hpp"

namespace veyon32api::core::internal { class VeyonCoreWrapper; }

namespace veyon32api::plugins {

// -----------------------------------------------------------------------
// ComputerPlugin — bridges Veyon's ComputerControlInterface and
// NetworkObjectDirectoryManager to the ComputerPluginInterface API.
// -----------------------------------------------------------------------
class ComputerPlugin final : public ComputerPluginInterface
{
public:
    explicit ComputerPlugin(core::internal::VeyonCoreWrapper& core);
    ~ComputerPlugin() override = default;

    VEYON32API_PLUGIN_METADATA(
        "a1b2c3d4-0001-0001-0001-000000000001",
        "ComputerPlugin",
        "Bridges Veyon ComputerControlInterface",
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
    core::internal::VeyonCoreWrapper& m_core;
};

} // namespace veyon32api::plugins
