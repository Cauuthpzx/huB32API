#include "PrecompiledHeader.hpp"
#include "internal/PluginRegistry.hpp"

namespace veyon32api::core::internal {

void PluginRegistry::registerPlugin(std::unique_ptr<PluginInterface> plugin)
{
    auto* raw = plugin.get();
    auto uid  = raw->uid();

    if (auto* p = dynamic_cast<ComputerPluginInterface*>(raw))
        m_computerPlugin = p;
    if (auto* p = dynamic_cast<FeaturePluginInterface*>(raw))
        m_featurePlugin = p;
    if (auto* p = dynamic_cast<SessionPluginInterface*>(raw))
        m_sessionPlugin = p;

    m_plugins[uid] = std::move(plugin);
    spdlog::debug("[PluginRegistry] registered plugin: {}", uid);
}

void PluginRegistry::initializeAll()
{
    for (auto& [uid, plugin] : m_plugins) {
        if (!plugin->initialize()) {
            spdlog::error("[PluginRegistry] plugin failed to initialize: {}", uid);
        }
    }
}

void PluginRegistry::shutdownAll()
{
    for (auto& [uid, plugin] : m_plugins) {
        plugin->shutdown();
    }
}

PluginInterface* PluginRegistry::find(const Uid& uid) const
{
    auto it = m_plugins.find(uid);
    return (it != m_plugins.end()) ? it->second.get() : nullptr;
}

ComputerPluginInterface*  PluginRegistry::computerPlugin() const { return m_computerPlugin; }
FeaturePluginInterface*   PluginRegistry::featurePlugin()  const { return m_featurePlugin;  }
SessionPluginInterface*   PluginRegistry::sessionPlugin()  const { return m_sessionPlugin;  }

const std::vector<PluginInterface*> PluginRegistry::all() const
{
    std::vector<PluginInterface*> result;
    result.reserve(m_plugins.size());
    for (auto& [uid, p] : m_plugins)
        result.push_back(p.get());
    return result;
}

} // namespace veyon32api::core::internal
