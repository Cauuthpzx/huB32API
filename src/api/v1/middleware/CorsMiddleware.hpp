#pragma once

#include <string>
#include <vector>

namespace httplib { class Request; class Response; }

namespace hub32api::api::v1::middleware {

struct CorsConfig {
    std::vector<std::string> allowedOrigins = {"*"};
    std::vector<std::string> allowedMethods = {"GET","POST","PUT","DELETE","OPTIONS"};
    std::vector<std::string> allowedHeaders = {"Authorization","Content-Type","X-Request-ID"};
    bool allowCredentials = false;
    int  maxAgeSec = hub32api::kDefaultCorsMaxAgeSec;  // seconds
};

class CorsMiddleware
{
public:
    explicit CorsMiddleware(CorsConfig cfg = CorsConfig{});

    // Adds CORS headers; returns false (and 204) for preflight OPTIONS requests
    bool process(const httplib::Request& req, httplib::Response& res);

private:
    CorsConfig m_cfg;
};

} // namespace hub32api::api::v1::middleware
