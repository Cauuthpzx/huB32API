#include "time_utils.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace hub32api::utils {

int64_t now_unix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t now_unix_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string format_iso8601(std::chrono::system_clock::time_point tp)
{
    const auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val{};
#ifdef _WIN32
    gmtime_s(&tm_val, &time_t_val);
#else
    gmtime_r(&time_t_val, &tm_val);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_val, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string format_iso8601_now()
{
    return format_iso8601(std::chrono::system_clock::now());
}

} // namespace hub32api::utils
