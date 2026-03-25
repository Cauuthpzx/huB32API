#pragma once

#include "hub32api/plugins/SessionPluginInterface.hpp"

namespace hub32api::core::internal { class Hub32CoreWrapper; }

namespace hub32api::plugins {

class SessionPlugin final : public SessionPluginInterface
{
public:
    explicit SessionPlugin(core::internal::Hub32CoreWrapper& core);

    HUB32API_PLUGIN_METADATA(
        "a1b2c3d4-0003-0003-0003-000000000003",
        "SessionPlugin",
        "Bridges Hub32 session and user information",
        "1.0.0"
    )

    Result<SessionInfo>           getSession(const Uid& computerUid) override;
    Result<UserInfo>              getUser(const Uid& computerUid) override;
    Result<std::vector<ScreenRect>> getScreens(const Uid& computerUid) override;

private:
    core::internal::Hub32CoreWrapper& m_core;
};

} // namespace hub32api::plugins
