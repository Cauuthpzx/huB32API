#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

#include "hub32api/export.h"

namespace hub32api {

// -----------------------------------------------------------------------
// Basic aliases
// -----------------------------------------------------------------------
using Uid       = std::string;   // UUID string, e.g. "ccb535a2-..."
using Port      = uint16_t;
using Timestamp = std::chrono::system_clock::time_point;

// -----------------------------------------------------------------------
// Computer state
// -----------------------------------------------------------------------
enum class HUB32API_EXPORT ComputerState
{
    Unknown,
    Offline,
    Online,
    Connecting,
    Connected,
    Disconnecting,
};

HUB32API_EXPORT std::string to_string(ComputerState state);
HUB32API_EXPORT ComputerState computer_state_from_string(const std::string& s);

// -----------------------------------------------------------------------
// Feature operation (mirrors Hub32 FeatureProviderInterface::Operation)
// -----------------------------------------------------------------------
enum class HUB32API_EXPORT FeatureOperation
{
    Start,
    Stop,
    Initialize,
};

HUB32API_EXPORT std::string to_string(FeatureOperation op);

// -----------------------------------------------------------------------
// Screen geometry
// -----------------------------------------------------------------------
struct HUB32API_EXPORT ScreenRect
{
    int x = 0;
    int y = 0;
    int width  = 0;
    int height = 0;
};

// -----------------------------------------------------------------------
// Image format for framebuffer endpoint
// -----------------------------------------------------------------------
enum class HUB32API_EXPORT ImageFormat
{
    Png,
    Jpeg,
};

HUB32API_EXPORT ImageFormat image_format_from_string(const std::string& s);
HUB32API_EXPORT std::string to_string(ImageFormat fmt);

} // namespace hub32api
