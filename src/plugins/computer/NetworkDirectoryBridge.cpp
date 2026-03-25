#include "core/PrecompiledHeader.hpp"
#include "NetworkDirectoryBridge.hpp"
#include "core/internal/Hub32CoreWrapper.hpp"

namespace hub32api::plugins {

/**
 * @brief Constructs the NetworkDirectoryBridge with a reference to the core wrapper.
 * @param core Reference to the Hub32CoreWrapper used for future Hub32Core integration.
 */
NetworkDirectoryBridge::NetworkDirectoryBridge(core::internal::Hub32CoreWrapper& core)
    : m_core(core) {}

/**
 * @brief Enumerates all computers known to the network directory.
 *
 * Returns a vector of ComputerInfo representing the discovered computers.
 * Currently provides realistic mock data simulating a mixed-environment
 * deployment with lab machines, office workstations, and a remote endpoint.
 *
 * When Hub32Core is linked, this will query
 * Hub32Core::networkObjectDirectoryManager() and filter by
 * NetworkObject::Type::Computer, mapping each result to a ComputerInfo.
 *
 * @return A vector of ComputerInfo entries for all discovered computers.
 */
std::vector<ComputerInfo> NetworkDirectoryBridge::enumerate() const
{
    // Mock data representing a realistic Veyon deployment.
    // ComputerState::Connected is used for "Locked" since the screen-lock
    // feature is a feature state rather than a connection state.
    return {
        ComputerInfo{
            "pc-001", "PC-Lab-01", "192.168.1.101", "Lab A",
            ComputerState::Online
        },
        ComputerInfo{
            "pc-002", "PC-Lab-02", "192.168.1.102", "Lab A",
            ComputerState::Online
        },
        ComputerInfo{
            "pc-003", "PC-Lab-03", "192.168.1.103", "Lab A",
            ComputerState::Offline
        },
        ComputerInfo{
            "pc-004", "PC-Office-01", "192.168.1.201", "Office",
            ComputerState::Online
        },
        ComputerInfo{
            "pc-005", "PC-Office-02", "192.168.1.202", "Office",
            ComputerState::Connected   // Represents a "locked" workstation
        },
        ComputerInfo{
            "pc-006", "PC-Remote-01", "10.0.0.50", "Remote",
            ComputerState::Unknown
        },
    };
}

} // namespace hub32api::plugins
