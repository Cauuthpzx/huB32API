#pragma once

#include <unordered_map>
#include "veyon32api/plugins/FeaturePluginInterface.hpp"

namespace veyon32api::plugins {

// Maps Veyon feature UIDs to FeatureDescriptor (avoids re-querying Veyon on each request)
class FeatureRegistry
{
public:
    void populate(const std::vector<FeatureDescriptor>& features);
    const FeatureDescriptor* find(const Uid& uid) const;
    const std::vector<FeatureDescriptor>& all() const;

private:
    std::vector<FeatureDescriptor>             m_features;
    std::unordered_map<Uid, size_t>            m_index;
};

} // namespace veyon32api::plugins
