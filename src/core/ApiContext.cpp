#include "PrecompiledHeader.hpp"
#include "internal/ApiContext.hpp"
#include "hub32api/core/Types.hpp"

namespace hub32api::core::internal {
// intentionally empty
} // namespace hub32api::core::internal

namespace hub32api {

/** @brief Converts FeatureOperation enum to string. */
std::string to_string(FeatureOperation op)
{
    switch (op) {
        case FeatureOperation::Start:      return "start";
        case FeatureOperation::Stop:       return "stop";
        case FeatureOperation::Initialize: return "initialize";
    }
    return "unknown";
}

/** @brief Converts ImageFormat enum to string. */
std::string to_string(ImageFormat fmt)
{
    switch (fmt) {
        case ImageFormat::Png:  return "png";
        case ImageFormat::Jpeg: return "jpeg";
    }
    return "png";
}

/** @brief Parses an image format string to enum. */
ImageFormat image_format_from_string(const std::string& s)
{
    if (s == "jpeg" || s == "jpg") return ImageFormat::Jpeg;
    return ImageFormat::Png;
}

} // namespace hub32api
