#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

#include "veyon32api/export.h"

namespace veyon32api {

// -----------------------------------------------------------------------
// Basic aliases
// -----------------------------------------------------------------------
using Uid       = std::string;   // UUID string, e.g. "ccb535a2-..."
using Port      = uint16_t;
using Timestamp = std::chrono::system_clock::time_point;

// -----------------------------------------------------------------------
// Computer state
// -----------------------------------------------------------------------
enum class VEYON32API_EXPORT ComputerState
{
    Unknown,
    Offline,
    Online,
    Connecting,
    Connected,
    Disconnecting,
};

VEYON32API_EXPORT std::string to_string(ComputerState state);
VEYON32API_EXPORT ComputerState computer_state_from_string(const std::string& s);

// -----------------------------------------------------------------------
// Feature operation (mirrors Veyon FeatureProviderInterface::Operation)
// -----------------------------------------------------------------------
enum class VEYON32API_EXPORT FeatureOperation
{
    Start,
    Stop,
    Initialize,
};

VEYON32API_EXPORT std::string to_string(FeatureOperation op);

// -----------------------------------------------------------------------
// Screen geometry
// -----------------------------------------------------------------------
struct VEYON32API_EXPORT ScreenRect
{
    int x = 0;
    int y = 0;
    int width  = 0;
    int height = 0;
};

// -----------------------------------------------------------------------
// Image format for framebuffer endpoint
// -----------------------------------------------------------------------
enum class VEYON32API_EXPORT ImageFormat
{
    Png,
    Jpeg,
};

VEYON32API_EXPORT ImageFormat image_format_from_string(const std::string& s);
VEYON32API_EXPORT std::string to_string(ImageFormat fmt);

} // namespace veyon32api
