/**
 * @file test_batch_controller.cpp
 * @brief Unit tests for hub32api BatchController (v2).
 *
 * These tests are DISABLED because BatchController methods operate on
 * httplib::Request and httplib::Response objects directly.  The v2 batch
 * endpoint accepts a JSON array of operations and delegates to
 * FeaturePluginInterface, but invoking it requires constructing valid
 * httplib request/response pairs, which is not feasible without a
 * running server or a mock/fake layer for these types.
 *
 * To enable these tests, either:
 *  1. Introduce a request/response abstraction that supports faking, or
 *  2. Promote to integration tests that hit the endpoint via HTTP.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

/// @brief Placeholder — requires httplib request/response mocks to implement.
TEST(BatchControllerV2, DISABLED_BatchLockAllComputers) {}
