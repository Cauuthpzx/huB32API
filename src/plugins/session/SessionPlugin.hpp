#pragma once

#include "veyon32api/plugins/SessionPluginInterface.hpp"

namespace veyon32api::core::internal { class VeyonCoreWrapper; }

namespace veyon32api::plugins {

class SessionPlugin final : public SessionPluginInterface
{
public:
    explicit SessionPlugin(core::internal::VeyonCoreWrapper& core);

    VEYON32API_PLUGIN_METADATA(
        "a1b2c3d4-0003-0003-0003-000000000003",
        "SessionPlugin",
        "Bridges Veyon session and user information",
        "1.0.0"
    )

    Result<SessionInfo>           getSession(const Uid& computerUid) override;
    Result<UserInfo>              getUser(const Uid& computerUid) override;
    Result<std::vector<ScreenRect>> getScreens(const Uid& computerUid) override;

private:
    core::internal::VeyonCoreWrapper& m_core;
};

} // namespace veyon32api::plugins
