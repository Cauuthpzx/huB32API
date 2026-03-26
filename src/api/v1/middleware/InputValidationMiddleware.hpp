#pragma once
#include <string>

namespace httplib { struct Request; struct Response; }

namespace hub32api::api::v1::middleware {

struct ValidationConfig
{
    size_t maxBodySize       = 1 * 1024 * 1024; // 1 MB default
    size_t maxFieldLength    = 1000;
    size_t maxArraySize      = 500;
    int    maxPathDepth      = 10;
};

class InputValidationMiddleware
{
public:
    explicit InputValidationMiddleware(ValidationConfig cfg = {});
    bool process(const httplib::Request& req, httplib::Response& res);
private:
    ValidationConfig m_cfg;
};

} // namespace hub32api::api::v1::middleware
