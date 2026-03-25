#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "hub32api/export.h"
#include "hub32api/plugins/PluginInterface.hpp"
#include "hub32api/plugins/ComputerPluginInterface.hpp"
#include "hub32api/plugins/FeaturePluginInterface.hpp"
#include "hub32api/plugins/SessionPluginInterface.hpp"

namespace hub32api::core::internal {

// -----------------------------------------------------------------------
// PluginRegistry — owns all plugin instances and provides typed lookups.
// Populated at startup; immutable after initialization.
// -----------------------------------------------------------------------
class HUB32API_EXPORT PluginRegistry
{
public:
    PluginRegistry() = default;

    void registerPlugin(std::unique_ptr<PluginInterface> plugin);
    void initializeAll();
    void shutdownAll();

    PluginInterface*          find(const Uid& uid) const;
    ComputerPluginInterface*  computerPlugin() const;
    FeaturePluginInterface*   featurePlugin() const;
    SessionPluginInterface*   sessionPlugin() const;

    const std::vector<PluginInterface*> all() const;

private:
    /// @brief Protects all mutable state for thread-safe concurrent access.
    mutable std::mutex m_mutex;

    std::unordered_map<Uid, std::unique_ptr<PluginInterface>> m_plugins;

    ComputerPluginInterface* m_computerPlugin = nullptr;
    FeaturePluginInterface*  m_featurePlugin  = nullptr;
    SessionPluginInterface*  m_sessionPlugin  = nullptr;
};

} // namespace hub32api::core::internal
