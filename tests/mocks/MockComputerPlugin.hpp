#pragma once

#include <gmock/gmock.h>
#include "veyon32api/plugins/ComputerPluginInterface.hpp"

namespace veyon32api {

class MockComputerPlugin : public ComputerPluginInterface
{
public:
    VEYON32API_PLUGIN_METADATA(
        "a1b2c3d4-0001-0001-0001-000000000001",
        "MockComputerPlugin", "Test mock", "0.0.1"
    )

    MOCK_METHOD(Result<std::vector<ComputerInfo>>, listComputers, (), (override));
    MOCK_METHOD(Result<ComputerInfo>, getComputer, (const Uid&), (override));
    MOCK_METHOD(Result<ComputerState>, getState, (const Uid&), (override));
    MOCK_METHOD(Result<FramebufferImage>, getFramebuffer,
                (const Uid&, int, int, ImageFormat), (override));
};

} // namespace veyon32api
