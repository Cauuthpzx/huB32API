#include "core/PrecompiledHeader.hpp"
#include "ComputerPlugin.hpp"
#include "core/internal/VeyonCoreWrapper.hpp"

namespace veyon32api::plugins {

ComputerPlugin::ComputerPlugin(core::internal::VeyonCoreWrapper& core)
    : m_core(core) {}

bool ComputerPlugin::initialize()
{
    // TODO: Access VeyonCore::networkObjectDirectoryManager() to enumerate computers
    spdlog::info("[ComputerPlugin] initialized (stub)");
    return true;
}

void ComputerPlugin::shutdown()
{
    spdlog::info("[ComputerPlugin] shutdown");
}

Result<std::vector<ComputerInfo>> ComputerPlugin::listComputers()
{
    // TODO: VeyonCore::networkObjectDirectoryManager().objects() → ComputerInfo list
    return Result<std::vector<ComputerInfo>>::fail(ApiError{
        ErrorCode::NotImplemented, "ComputerPlugin::listComputers not implemented"
    });
}

Result<ComputerInfo> ComputerPlugin::getComputer(const Uid& uid)
{
    // TODO: lookup by uid in directory
    return Result<ComputerInfo>::fail(ApiError{ErrorCode::ComputerNotFound, uid});
}

Result<ComputerState> ComputerPlugin::getState(const Uid& uid)
{
    // TODO: ComputerControlInterface::state()
    return Result<ComputerState>::ok(ComputerState::Unknown);
}

Result<FramebufferImage> ComputerPlugin::getFramebuffer(
    const Uid& uid, int width, int height, ImageFormat fmt)
{
    // TODO: ComputerControlInterface::scaledFramebuffer() → encode to fmt
    return Result<FramebufferImage>::fail(ApiError{
        ErrorCode::FramebufferNotAvailable, "stub"
    });
}

} // namespace veyon32api::plugins
