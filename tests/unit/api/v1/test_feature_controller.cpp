/**
 * @file test_feature_controller.cpp
 * @brief Unit tests for hub32api FeatureController (v1).
 *
 * These tests are DISABLED because FeatureController handler methods
 * operate on httplib::Request and httplib::Response objects directly.
 * There is no lightweight mock/fake for these types available in the
 * current test infrastructure, so the controller cannot be unit-tested
 * without a running HTTP server.
 *
 * To enable these tests, either:
 *  1. Add a request/response abstraction that can be faked, or
 *  2. Promote to integration tests using a real loopback HTTP client.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

/// @brief Placeholder — requires httplib request/response mocks to implement.
TEST(FeatureControllerV1, DISABLED_ListFeaturesForComputer) {}

/// @brief Placeholder — requires httplib request/response mocks to implement.
TEST(FeatureControllerV1, DISABLED_ControlFeatureStartsFeature) {}
