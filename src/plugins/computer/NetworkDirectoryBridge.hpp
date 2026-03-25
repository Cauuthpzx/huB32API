#pragma once

#include <vector>
#include "hub32api/plugins/ComputerPluginInterface.hpp"

namespace hub32api::core::internal { class Hub32CoreWrapper; }

namespace hub32api::plugins {

// -----------------------------------------------------------------------
// NetworkDirectoryBridge — translates Hub32's NetworkObjectDirectory
// into the flat ComputerInfo list used by the API.
// -----------------------------------------------------------------------
class NetworkDirectoryBridge
{
public:
    explicit NetworkDirectoryBridge(core::internal::Hub32CoreWrapper& core);

    std::vector<ComputerInfo> enumerate() const;

private:
    core::internal::Hub32CoreWrapper& m_core;
};

} // namespace hub32api::plugins
