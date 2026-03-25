#include <gtest/gtest.h>

// Integration tests for v1 API.
// These spin up a real HttpServer (with mock VeyonCore) and send HTTP requests via cpp-httplib.

// TODO: Implement after core plugin stubs are wired in.
// Pattern:
//   1. Create ServerConfig with httpPort=0 (random)
//   2. Start HttpServer in background thread
//   3. Send requests with httplib::Client
//   4. Assert response status and JSON body
//   5. Stop server

TEST(V1ApiIntegration, DISABLED_HealthEndpointReturns200)
{
    // TODO
}

TEST(V1ApiIntegration, DISABLED_AuthLoginWithValidCredentials)
{
    // TODO
}

TEST(V1ApiIntegration, DISABLED_ComputerListRequiresAuth)
{
    // TODO: 401 without Bearer token
}
