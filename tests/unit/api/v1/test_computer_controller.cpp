/**
 * @file test_computer_controller.cpp
 * @brief Unit tests for hub32api ComputerController (v1).
 *
 * These tests are DISABLED because ComputerController methods accept
 * httplib::Request and httplib::Response parameters directly.  Without
 * a mock or fake for these httplib types, the controller logic cannot
 * be invoked in isolation.
 *
 * To enable these tests, either:
 *  1. Introduce a thin abstraction over httplib::Request/Response that
 *     can be stubbed in unit tests, or
 *  2. Move them to integration tests that start the HttpServer on a
 *     loopback port and exercise the endpoints via an HTTP client.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

/// @brief Placeholder — requires httplib request/response mocks to implement.
TEST(ComputerControllerV1, DISABLED_ListReturnsMockData) {}

/// @brief Placeholder — requires httplib request/response mocks to implement.
TEST(ComputerControllerV1, DISABLED_GetOneNotFoundReturns404) {}
