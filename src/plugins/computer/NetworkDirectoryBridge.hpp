#pragma once

#include <vector>
#include "veyon32api/plugins/ComputerPluginInterface.hpp"

namespace veyon32api::core::internal { class VeyonCoreWrapper; }

namespace veyon32api::plugins {

// -----------------------------------------------------------------------
// NetworkDirectoryBridge — translates Veyon's NetworkObjectDirectory
// into the flat ComputerInfo list used by the API.
// -----------------------------------------------------------------------
class NetworkDirectoryBridge
{
public:
    explicit NetworkDirectoryBridge(core::internal::VeyonCoreWrapper& core);

    std::vector<ComputerInfo> enumerate() const;

private:
    core::internal::VeyonCoreWrapper& m_core;
};

} // namespace veyon32api::plugins
