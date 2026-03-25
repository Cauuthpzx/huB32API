#pragma once

#include <vector>
#include <string>
#include "veyon32api/plugins/PluginInterface.hpp"
#include "veyon32api/core/Types.hpp"
#include "veyon32api/core/Result.hpp"

namespace veyon32api {

// Forward declarations
struct ComputerInfo;
struct FramebufferImage;

// -----------------------------------------------------------------------
// ComputerInfo — data returned by computer discovery
// -----------------------------------------------------------------------
struct VEYON32API_EXPORT ComputerInfo
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
struct VEYON32API_EXPORT FramebufferImage
{
    std::vector<uint8_t> data;
    ImageFormat          format = ImageFormat::Png;
    int                  width  = 0;
    int                  height = 0;
};

// -----------------------------------------------------------------------
// ComputerPluginInterface — wraps Veyon ComputerControlInterface
// -----------------------------------------------------------------------
class VEYON32API_EXPORT ComputerPluginInterface : public PluginInterface
{
public:
    virtual Result<std::vector<ComputerInfo>> listComputers() = 0;
    virtual Result<ComputerInfo>   getComputer(const Uid& uid) = 0;
    virtual Result<ComputerState>  getState(const Uid& uid) = 0;
    virtual Result<FramebufferImage> getFramebuffer(
        const Uid& uid, int width, int height, ImageFormat fmt) = 0;
};

} // namespace veyon32api
