/**
 * @file test_command_dispatcher.cpp
 * @brief Unit tests for the CommandDispatcher class.
 *
 * Tests handler registration, successful dispatch, unknown feature
 * handling, and exception propagation from handlers.
 */

#include <gtest/gtest.h>
#include "hub32agent/CommandDispatcher.hpp"
#include "hub32agent/FeatureHandler.hpp"
#include "hub32agent/AgentClient.hpp"  // for PendingCommand

using namespace hub32agent;

namespace {

/**
 * @brief Mock feature handler for testing the dispatcher.
 *
 * Returns a JSON result for normal operations and throws
 * std::runtime_error when the operation is "fail".
 */
class MockHandler : public FeatureHandler
{
public:
    /**
     * @brief Returns the mock feature UID.
     * @return "test-feature".
     */
    std::string featureUid() const override { return "test-feature"; }

    /**
     * @brief Returns the mock feature name.
     * @return "Test Feature".
     */
    std::string name() const override { return "Test Feature"; }

    /**
     * @brief Executes the mock operation.
     *
     * If operation is "fail", throws std::runtime_error.
     * Otherwise returns a JSON object with the operation name.
     *
     * @param operation The operation to perform.
     * @param args      Arguments (unused in mock).
     * @return JSON result string.
     * @throws std::runtime_error if operation is "fail".
     */
    std::string execute(const std::string& operation,
                        const std::map<std::string, std::string>& /*args*/) override
    {
        if (operation == "fail") {
            throw std::runtime_error("intentional failure");
        }
        return R"({"status":"ok","op":")" + operation + R"("})";
    }
};

} // anonymous namespace

/**
 * @brief Tests that a handler can be registered and dispatched to.
 *
 * Registers a MockHandler, verifies it appears in registeredFeatures(),
 * then dispatches a command and checks the result is successful.
 */
TEST(CommandDispatcherTest, RegisterAndDispatch)
{
    CommandDispatcher dispatcher;
    dispatcher.registerHandler(std::make_unique<MockHandler>());

    auto features = dispatcher.registeredFeatures();
    ASSERT_EQ(features.size(), 1u);
    EXPECT_EQ(features[0], "test-feature");

    PendingCommand cmd;
    cmd.commandId  = "c1";
    cmd.featureUid = "test-feature";
    cmd.operation  = "start";

    auto result = dispatcher.dispatch(cmd);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.result.empty());
    EXPECT_GE(result.durationMs, 0);
}

/**
 * @brief Tests that dispatching to an unknown feature returns failure.
 *
 * Dispatches a command with a featureUid that has no registered handler
 * and verifies the result indicates failure with an appropriate message.
 */
TEST(CommandDispatcherTest, UnknownFeatureReturnsFalse)
{
    CommandDispatcher dispatcher;

    PendingCommand cmd;
    cmd.featureUid = "nonexistent";
    cmd.operation  = "start";

    auto result = dispatcher.dispatch(cmd);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.result.find("Unknown"), std::string::npos);
}

/**
 * @brief Tests that handler exceptions are caught and reported.
 *
 * Dispatches a command that causes the MockHandler to throw, and
 * verifies the exception message is captured in the result.
 */
TEST(CommandDispatcherTest, HandlerExceptionCaught)
{
    CommandDispatcher dispatcher;
    dispatcher.registerHandler(std::make_unique<MockHandler>());

    PendingCommand cmd;
    cmd.featureUid = "test-feature";
    cmd.operation  = "fail";

    auto result = dispatcher.dispatch(cmd);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.result.find("intentional"), std::string::npos);
}
