/**
 * @file test_auth_controller.cpp
 * @brief Unit tests for hub32api AuthController (v1).
 *
 * These tests are DISABLED because AuthController::login() and logout()
 * operate directly on httplib::Request and httplib::Response objects.
 * Without a running HTTP server or a proper mock/fake for the httplib
 * request/response types, these endpoints cannot be exercised in a pure
 * unit-test environment.
 *
 * To enable these tests, either:
 *  1. Introduce a thin abstraction layer over httplib::Request/Response
 *     that can be faked in tests, or
 *  2. Promote them to integration tests that spin up the HttpServer on
 *     a loopback port and use an HTTP client to hit the endpoints.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

/// @brief Placeholder — requires httplib request/response mocks to implement.
TEST(AuthControllerV1, DISABLED_LoginWithValidKeyReturnsToken) {}

/// @brief Placeholder — requires httplib request/response mocks to implement.
TEST(AuthControllerV1, DISABLED_LoginWithInvalidKeyReturns401) {}
