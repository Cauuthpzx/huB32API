#include "core/PrecompiledHeader.hpp"
#include "FeatureRegistry.hpp"

namespace hub32api::plugins {

void FeatureRegistry::populate(const std::vector<FeatureDescriptor>& features)
{
    m_features = features;
    m_index.clear();
    for (size_t i = 0; i < m_features.size(); ++i)
        m_index[m_features[i].uid] = i;
}

const FeatureDescriptor* FeatureRegistry::find(const Uid& uid) const
{
    auto it = m_index.find(uid);
    if (it == m_index.end()) return nullptr;
    return &m_features[it->second];
}

const std::vector<FeatureDescriptor>& FeatureRegistry::all() const
{
    return m_features;
}

} // namespace hub32api::plugins
