#pragma once

namespace httplib { class Request; class Response; }

namespace veyon32api::api::v1::middleware {

// Request/response access logging via spdlog
class LoggingMiddleware
{
public:
    void logRequest (const httplib::Request& req);
    void logResponse(const httplib::Request& req, const httplib::Response& res);
};

} // namespace veyon32api::api::v1::middleware
