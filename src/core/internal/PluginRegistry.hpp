#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include "veyon32api/export.h"
#include "veyon32api/plugins/PluginInterface.hpp"
#include "veyon32api/plugins/ComputerPluginInterface.hpp"
#include "veyon32api/plugins/FeaturePluginInterface.hpp"
#include "veyon32api/plugins/SessionPluginInterface.hpp"

namespace veyon32api::core::internal {

// -----------------------------------------------------------------------
// PluginRegistry — owns all plugin instances and provides typed lookups.
// Populated at startup; immutable after initialization.
// -----------------------------------------------------------------------
class VEYON32API_EXPORT PluginRegistry
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
    std::unordered_map<Uid, std::unique_ptr<PluginInterface>> m_plugins;

    ComputerPluginInterface* m_computerPlugin = nullptr;
    FeaturePluginInterface*  m_featurePlugin  = nullptr;
    SessionPluginInterface*  m_sessionPlugin  = nullptr;
};

} // namespace veyon32api::core::internal
