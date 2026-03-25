#include <gtest/gtest.h>
#include "core/internal/PluginRegistry.hpp"
#include "MockComputerPlugin.hpp"

using namespace hub32api::core::internal;

TEST(PluginRegistryTest, RegisterAndFind)
{
    PluginRegistry reg;
    auto mock = std::make_unique<hub32api::MockComputerPlugin>();
    auto* rawPtr = mock.get();
    reg.registerPlugin(std::move(mock));

    EXPECT_EQ(reg.computerPlugin(), rawPtr);
    EXPECT_NE(reg.find("a1b2c3d4-0001-0001-0001-000000000001"), nullptr);
}

TEST(PluginRegistryTest, FindUnknownUidReturnsNull)
{
    PluginRegistry reg;
    EXPECT_EQ(reg.find("no-such-uid"), nullptr);
}
