#include "../../core/PrecompiledHeader.hpp"
#include "HttpErrorUtil.hpp"
#include <httplib.h>

namespace hub32api::api::common {

void sendError(httplib::Response& res, int status,
               const std::string& title, const std::string& detail)
{
    nlohmann::json j;
    j["status"] = status;
    j["title"]  = title;
    j["detail"] = detail.empty() ? title : detail;
    res.status  = status;
    res.set_content(j.dump(), "application/problem+json");
}

} // namespace hub32api::api::common
