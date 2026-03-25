/**
 * @file test_feature_plugin.cpp
 * @brief Unit tests for hub32api::plugins::FeaturePlugin via a mock
 *        FeaturePluginInterface implementation.
 *
 * The real FeaturePlugin delegates to Hub32CoreWrapper's FeatureManager,
 * which requires the full Hub32 core + Qt runtime.  To test the plugin
 * interface contract in isolation, we use a MockFeaturePlugin that
 * maintains in-memory feature state and supports Start/Stop operations.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "hub32api/plugins/FeaturePluginInterface.hpp"
#include "hub32api/core/Error.hpp"

using namespace hub32api;

namespace {

// ---------------------------------------------------------------------------
// MockFeaturePlugin — provides 5 features with Start/Stop tracking
// ---------------------------------------------------------------------------

/**
 * @brief A test-only FeaturePluginInterface that provides 5 mock features
 *        and tracks which (computer, feature) pairs are currently active.
 */
class MockFeaturePlugin final : public FeaturePluginInterface
{
public:
    /// @brief Plugin UID for the mock.
    Uid         uid()         const override { return "mock-feature-plugin-uid"; }
    /// @brief Plugin name.
    std::string name()        const override { return "MockFeaturePlugin"; }
    /// @brief Plugin description.
    std::string description() const override { return "Mock feature plugin for unit tests"; }
    /// @brief Plugin version.
    std::string version()     const override { return "0.0.1"; }

    /**
     * @brief Returns 5 mock FeatureDescriptor entries.
     * @param computerUid  Ignored — the same feature set is returned for all computers.
     * @return Result containing a vector of 5 FeatureDescriptor objects.
     */
    Result<std::vector<FeatureDescriptor>> listFeatures(const Uid& /*computerUid*/) override
    {
        return Result<std::vector<FeatureDescriptor>>::ok({
            { "feat-screen-lock",     "", "ScreenLock",     "Lock student screens",        false, true, false, "" },
            { "feat-input-lock",      "", "InputLock",      "Lock keyboard and mouse",     false, true, false, "" },
            { "feat-screen-broadcast","", "ScreenBroadcast","Broadcast teacher screen",    false, true, false, "" },
            { "feat-text-message",    "", "TextMessage",    "Send text message to student", false, true, false, "" },
            { "feat-power-control",   "", "PowerControl",   "Power on/off/reboot",         false, true, true,  "" },
        });
    }

    /**
     * @brief Checks whether a specific feature is active on a computer.
     * @param computerUid  The target computer UID.
     * @param featureUid   The feature UID.
     * @return Result<bool> — true if the feature was started and not stopped.
     */
    Result<bool> isFeatureActive(const Uid& computerUid, const Uid& featureUid) override
    {
        const std::string key = computerUid + "|" + featureUid;
        bool active = m_activeFeatures.count(key) > 0;
        return Result<bool>::ok(active);
    }

    /**
     * @brief Starts or stops a feature on a computer.
     *
     * Start adds the (computer, feature) pair to the active set;
     * Stop removes it.
     *
     * @param computerUid  The target computer UID.
     * @param featureUid   The feature UID.
     * @param op           Start or Stop.
     * @param args         Additional arguments (unused in mock).
     * @return Result<void> — always succeeds.
     */
    Result<void> controlFeature(const Uid& computerUid, const Uid& featureUid,
                                 FeatureOperation op, const FeatureArgs& /*args*/) override
    {
        const std::string key = computerUid + "|" + featureUid;
        if (op == FeatureOperation::Start) {
            m_activeFeatures.insert(key);
        } else if (op == FeatureOperation::Stop) {
            m_activeFeatures.erase(key);
        }
        return Result<void>::ok();
    }

    /**
     * @brief Batch-controls a feature across multiple computers.
     * @param computerUids  List of computer UIDs to target.
     * @param featureUid    The feature UID.
     * @param op            Start or Stop.
     * @param args          Additional arguments (unused in mock).
     * @return Result containing the list of successfully processed UIDs.
     */
    Result<std::vector<Uid>> controlFeatureBatch(
        const std::vector<Uid>& computerUids, const Uid& featureUid,
        FeatureOperation op, const FeatureArgs& args) override
    {
        std::vector<Uid> successes;
        for (const auto& uid : computerUids) {
            auto r = controlFeature(uid, featureUid, op, args);
            if (r.is_ok()) {
                successes.push_back(uid);
            }
        }
        return Result<std::vector<Uid>>::ok(std::move(successes));
    }

private:
    std::unordered_set<std::string> m_activeFeatures;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// ListFeatures — mock returns exactly 5 features
// ---------------------------------------------------------------------------

/**
 * @brief Verifies that the mock plugin returns exactly 5 feature descriptors.
 */
TEST(FeaturePluginTest, ListFeaturesReturnsFiveEntries)
{
    MockFeaturePlugin plugin;
    auto result = plugin.listFeatures("any-computer");

    ASSERT_TRUE(result.is_ok()) << "listFeatures must succeed";
    EXPECT_EQ(result.value().size(), 5u)
        << "Mock plugin should expose exactly 5 features";
}

// ---------------------------------------------------------------------------
// ControlFeature Start — sets feature active
// ---------------------------------------------------------------------------

/**
 * @brief Starts a feature on a computer and verifies isFeatureActive
 *        returns true.
 */
TEST(FeaturePluginTest, ControlFeatureStartMakesFeatureActive)
{
    MockFeaturePlugin plugin;

    // Initially not active.
    auto beforeResult = plugin.isFeatureActive("pc-001", "feat-screen-lock");
    ASSERT_TRUE(beforeResult.is_ok());
    EXPECT_FALSE(beforeResult.value()) << "Feature should be inactive before Start";

    // Start the feature.
    auto startResult = plugin.controlFeature(
        "pc-001", "feat-screen-lock", FeatureOperation::Start, {});
    ASSERT_TRUE(startResult.is_ok()) << "controlFeature(Start) must succeed";

    // Now it should be active.
    auto afterResult = plugin.isFeatureActive("pc-001", "feat-screen-lock");
    ASSERT_TRUE(afterResult.is_ok());
    EXPECT_TRUE(afterResult.value()) << "Feature should be active after Start";
}

// ---------------------------------------------------------------------------
// ControlFeature Stop — clears feature active state
// ---------------------------------------------------------------------------

/**
 * @brief Starts a feature, then stops it, and verifies isFeatureActive
 *        returns false.
 */
TEST(FeaturePluginTest, ControlFeatureStopMakesFeatureInactive)
{
    MockFeaturePlugin plugin;

    // Start the feature first.
    plugin.controlFeature("pc-002", "feat-input-lock", FeatureOperation::Start, {});

    auto activeResult = plugin.isFeatureActive("pc-002", "feat-input-lock");
    ASSERT_TRUE(activeResult.is_ok());
    EXPECT_TRUE(activeResult.value()) << "Feature must be active after Start";

    // Stop the feature.
    auto stopResult = plugin.controlFeature(
        "pc-002", "feat-input-lock", FeatureOperation::Stop, {});
    ASSERT_TRUE(stopResult.is_ok()) << "controlFeature(Stop) must succeed";

    // Verify it is now inactive.
    auto afterResult = plugin.isFeatureActive("pc-002", "feat-input-lock");
    ASSERT_TRUE(afterResult.is_ok());
    EXPECT_FALSE(afterResult.value()) << "Feature should be inactive after Stop";
}
