#include "PrecompiledHeader.hpp"
#include "internal/PluginRegistry.hpp"

namespace hub32api::core::internal {

void PluginRegistry::registerPlugin(std::unique_ptr<PluginInterface> plugin)
{
    std::lock_guard lock(m_mutex);
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
    std::lock_guard lock(m_mutex);
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ) {
        auto& [uid, plugin] = *it;
        if (!plugin->initialize()) {
            spdlog::error("[PluginRegistry] plugin failed to initialize, removing: {}", uid);
            // Clear typed pointers if they reference this plugin
            if (dynamic_cast<ComputerPluginInterface*>(plugin.get()) == m_computerPlugin)
                m_computerPlugin = nullptr;
            if (dynamic_cast<FeaturePluginInterface*>(plugin.get()) == m_featurePlugin)
                m_featurePlugin = nullptr;
            if (dynamic_cast<SessionPluginInterface*>(plugin.get()) == m_sessionPlugin)
                m_sessionPlugin = nullptr;
            it = m_plugins.erase(it);
        } else {
            ++it;
        }
    }
}

void PluginRegistry::shutdownAll()
{
    std::lock_guard lock(m_mutex);
    for (auto& [uid, plugin] : m_plugins) {
        plugin->shutdown();
    }
}

PluginInterface* PluginRegistry::find(const Uid& uid) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_plugins.find(uid);
    return (it != m_plugins.end()) ? it->second.get() : nullptr;
}

ComputerPluginInterface* PluginRegistry::computerPlugin() const
{
    std::lock_guard lock(m_mutex);
    return m_computerPlugin;
}

FeaturePluginInterface* PluginRegistry::featurePlugin() const
{
    std::lock_guard lock(m_mutex);
    return m_featurePlugin;
}

SessionPluginInterface* PluginRegistry::sessionPlugin() const
{
    std::lock_guard lock(m_mutex);
    return m_sessionPlugin;
}

const std::vector<PluginInterface*> PluginRegistry::all() const
{
    std::lock_guard lock(m_mutex);
    std::vector<PluginInterface*> result;
    result.reserve(m_plugins.size());
    for (const auto& [uid, p] : m_plugins)
        result.push_back(p.get());
    return result;
}

} // namespace hub32api::core::internal
