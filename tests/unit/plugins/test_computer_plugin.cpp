/**
 * @file test_computer_plugin.cpp
 * @brief Unit tests for hub32api::plugins::ComputerPlugin via a mock
 *        ComputerPluginInterface implementation.
 *
 * The real ComputerPlugin delegates to Hub32CoreWrapper, which requires
 * Qt and the Hub32 core library.  To test the plugin interface contract
 * in isolation, we use a MockComputerPlugin that returns deterministic
 * mock data (6 computers in a lab layout).
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include <string>
#include <algorithm>

#include "hub32api/plugins/ComputerPluginInterface.hpp"
#include "hub32api/core/Error.hpp"

using namespace hub32api;

namespace {

// ---------------------------------------------------------------------------
// MockComputerPlugin — returns canned lab data for unit testing
// ---------------------------------------------------------------------------

/**
 * @brief A test-only ComputerPluginInterface that provides 6 mock computer
 *        entries, simulating a classroom discovery result.
 */
class MockComputerPlugin final : public ComputerPluginInterface
{
public:
    /// @brief Plugin UID for the mock.
    Uid         uid()         const override { return "mock-computer-plugin-uid"; }
    /// @brief Plugin name.
    std::string name()        const override { return "MockComputerPlugin"; }
    /// @brief Plugin description.
    std::string description() const override { return "Mock plugin for unit tests"; }
    /// @brief Plugin version.
    std::string version()     const override { return "0.0.1"; }

    /**
     * @brief Returns 6 mock ComputerInfo entries representing a lab.
     * @return Result containing a vector of 6 ComputerInfo objects.
     */
    Result<std::vector<ComputerInfo>> listComputers() override
    {
        return Result<std::vector<ComputerInfo>>::ok({
            { "pc-001", "PC-Lab-01", "192.168.1.101", "Lab-A", ComputerState::Online },
            { "pc-002", "PC-Lab-02", "192.168.1.102", "Lab-A", ComputerState::Online },
            { "pc-003", "PC-Lab-03", "192.168.1.103", "Lab-A", ComputerState::Offline },
            { "pc-004", "PC-Lab-04", "192.168.1.104", "Lab-B", ComputerState::Online },
            { "pc-005", "PC-Lab-05", "192.168.1.105", "Lab-B", ComputerState::Connecting },
            { "pc-006", "PC-Lab-06", "192.168.1.106", "Lab-B", ComputerState::Unknown },
        });
    }

    /**
     * @brief Returns the ComputerInfo for the given UID, or ComputerNotFound.
     * @param uid  The computer UID to look up.
     * @return Result with the matching ComputerInfo or an error.
     */
    Result<ComputerInfo> getComputer(const Uid& uid) override
    {
        auto all = listComputers();
        if (all.is_err()) return Result<ComputerInfo>::fail(all.error());

        for (const auto& c : all.value()) {
            if (c.uid == uid) {
                return Result<ComputerInfo>::ok(c);
            }
        }
        return Result<ComputerInfo>::fail(
            ApiError{ ErrorCode::ComputerNotFound, "Computer not found: " + uid });
    }

    /**
     * @brief Returns Unknown state for any UID (mock).
     */
    Result<ComputerState> getState(const Uid& /*uid*/) override
    {
        return Result<ComputerState>::ok(ComputerState::Unknown);
    }

    /**
     * @brief Returns FramebufferNotAvailable (mock has no real screen data).
     */
    Result<FramebufferImage> getFramebuffer(
        const Uid& /*uid*/, int /*width*/, int /*height*/, ImageFormat /*fmt*/) override
    {
        return Result<FramebufferImage>::fail(
            ApiError{ ErrorCode::FramebufferNotAvailable, "mock" });
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// ListComputers — mock returns exactly 6 entries
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that the mock plugin returns exactly 6 computer entries.
 */
TEST(ComputerPluginTest, ListComputersReturnsSixEntries)
{
    MockComputerPlugin plugin;
    auto result = plugin.listComputers();

    ASSERT_TRUE(result.is_ok()) << "listComputers must succeed";
    EXPECT_EQ(result.value().size(), 6u)
        << "Mock lab should contain exactly 6 computers";
}

// ---------------------------------------------------------------------------
// GetComputer — lookup by known UID
// ---------------------------------------------------------------------------

/**
 * @brief Looks up pc-001 and verifies the name is "PC-Lab-01".
 */
TEST(ComputerPluginTest, GetComputerReturnsCorrectName)
{
    MockComputerPlugin plugin;
    auto result = plugin.getComputer("pc-001");

    ASSERT_TRUE(result.is_ok()) << "getComputer(pc-001) must succeed";
    EXPECT_EQ(result.value().name, "PC-Lab-01");
    EXPECT_EQ(result.value().hostname, "192.168.1.101");
    EXPECT_EQ(result.value().location, "Lab-A");
}

// ---------------------------------------------------------------------------
// GetComputer — lookup with nonexistent UID returns error
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that looking up a nonexistent UID produces a
 *        ComputerNotFound error.
 */
TEST(ComputerPluginTest, GetComputerNonexistentReturnsError)
{
    MockComputerPlugin plugin;
    auto result = plugin.getComputer("nonexistent");

    ASSERT_TRUE(result.is_err()) << "getComputer(nonexistent) must fail";
    EXPECT_EQ(result.error().code, ErrorCode::ComputerNotFound);
}
