#include "core/PrecompiledHeader.hpp"
#include "SessionPlugin.hpp"
#include "core/internal/VeyonCoreWrapper.hpp"

namespace veyon32api::plugins {

SessionPlugin::SessionPlugin(core::internal::VeyonCoreWrapper& core)
    : m_core(core) {}

Result<SessionInfo> SessionPlugin::getSession(const Uid& /*computerUid*/)
{
    // TODO: ComputerControlInterface::sessionInfo() → SessionInfo
    return Result<SessionInfo>::fail(ApiError{ErrorCode::NotImplemented, "getSession"});
}

Result<UserInfo> SessionPlugin::getUser(const Uid& /*computerUid*/)
{
    // TODO: ComputerControlInterface::userLoginName() / userFullName()
    return Result<UserInfo>::fail(ApiError{ErrorCode::NotImplemented, "getUser"});
}

Result<std::vector<ScreenRect>> SessionPlugin::getScreens(const Uid& /*computerUid*/)
{
    // TODO: ComputerControlInterface::screens() → vector<ScreenRect>
    return Result<std::vector<ScreenRect>>::ok({});
}

} // namespace veyon32api::plugins
