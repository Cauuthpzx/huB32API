#include "core/PrecompiledHeader.hpp"
#include "NetworkDirectoryBridge.hpp"
#include "core/internal/VeyonCoreWrapper.hpp"

namespace veyon32api::plugins {

NetworkDirectoryBridge::NetworkDirectoryBridge(core::internal::VeyonCoreWrapper& core)
    : m_core(core) {}

std::vector<ComputerInfo> NetworkDirectoryBridge::enumerate() const
{
    // TODO: VeyonCore::networkObjectDirectoryManager() → iterate objects
    // TODO: filter by NetworkObject::Type::Computer
    // TODO: map to ComputerInfo{uid, name, hostname, location, Unknown}
    return {};
}

} // namespace veyon32api::plugins
