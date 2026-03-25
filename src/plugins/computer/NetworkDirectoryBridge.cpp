#include "core/PrecompiledHeader.hpp"
#include "NetworkDirectoryBridge.hpp"
#include "core/internal/Hub32CoreWrapper.hpp"

namespace hub32api::plugins {

NetworkDirectoryBridge::NetworkDirectoryBridge(core::internal::Hub32CoreWrapper& core)
    : m_core(core) {}

std::vector<ComputerInfo> NetworkDirectoryBridge::enumerate() const
{
    // TODO: Hub32Core::networkObjectDirectoryManager() → iterate objects
    // TODO: filter by NetworkObject::Type::Computer
    // TODO: map to ComputerInfo{uid, name, hostname, location, Unknown}
    return {};
}

} // namespace hub32api::plugins
