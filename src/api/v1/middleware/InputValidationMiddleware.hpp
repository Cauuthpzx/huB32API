#pragma once
#include <string>
#include <nlohmann/json_fwd.hpp>

#include "hub32api/core/Constants.hpp"

namespace httplib { struct Request; struct Response; }

namespace hub32api::api::v1::middleware {

struct ValidationConfig
{
    size_t maxBodySize       = hub32api::kDefaultMaxBodySize;      // bytes — 1 MB
    size_t maxFieldLength    = hub32api::kDefaultMaxFieldLength;   // characters
    size_t maxArraySize      = hub32api::kDefaultMaxArraySize;     // elements
    int    maxPathDepth      = hub32api::kDefaultMaxPathDepth;     // levels
};

class InputValidationMiddleware
{
public:
    explicit InputValidationMiddleware(ValidationConfig cfg = {});
    bool process(const httplib::Request& req, httplib::Response& res);
private:
    bool validateJsonValue(const nlohmann::json& j, int depth, std::string& violation) const;
    ValidationConfig m_cfg;
};

} // namespace hub32api::api::v1::middleware
