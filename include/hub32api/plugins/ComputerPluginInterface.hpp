#pragma once

#include <vector>
#include <string>
#include "hub32api/plugins/PluginInterface.hpp"
#include "hub32api/core/Types.hpp"
#include "hub32api/core/Result.hpp"

namespace hub32api {

// Forward declarations
struct ComputerInfo;
struct FramebufferImage;

// -----------------------------------------------------------------------
// ComputerInfo — data returned by computer discovery
// -----------------------------------------------------------------------
struct HUB32API_EXPORT ComputerInfo
{
    Uid         uid;
    std::string name;
    std::string hostname;
    std::string location;    // classroom / group name
    ComputerState state = ComputerState::Unknown;
};

// -----------------------------------------------------------------------
// FramebufferImage — raw image bytes from screen capture
// -----------------------------------------------------------------------
struct HUB32API_EXPORT FramebufferImage
{
    std::vector<uint8_t> data;
    ImageFormat          format = ImageFormat::Png;
    int                  width  = 0;
    int                  height = 0;
    int compression = -1;  // PNG compression 0-9, -1 = default
    int quality     = -1;  // JPEG quality 0-100, -1 = default
};

// -----------------------------------------------------------------------
// ComputerPluginInterface — wraps Hub32 ComputerControlInterface
// -----------------------------------------------------------------------
class HUB32API_EXPORT ComputerPluginInterface : public PluginInterface
{
public:
    virtual Result<std::vector<ComputerInfo>> listComputers() = 0;
    virtual Result<ComputerInfo>   getComputer(const Uid& uid) = 0;
    virtual Result<ComputerState>  getState(const Uid& uid) = 0;
    virtual Result<FramebufferImage> getFramebuffer(
        const Uid& uid, int width, int height, ImageFormat fmt,
        int compression = -1, int quality = -1) = 0;
};

} // namespace hub32api
