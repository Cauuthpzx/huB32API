#pragma once

#include <string>
#include "hub32api/core/Types.hpp"
#include "hub32api/export.h"

namespace hub32api {

// -----------------------------------------------------------------------
// PluginInterface — base class for all hub32api plugins.
// Naming mirrors Hub32's PluginInterface (no I-prefix, per convention).
// -----------------------------------------------------------------------
class HUB32API_EXPORT PluginInterface
{
public:
    virtual ~PluginInterface() = default;

    virtual Uid         uid()         const = 0;
    virtual std::string name()        const = 0;
    virtual std::string description() const = 0;
    virtual std::string version()     const = 0;

    // Called once after all plugins are registered
    virtual bool initialize() { return true; }
    // Called on clean shutdown
    virtual void shutdown()   {}
};

// Use this macro in every plugin .cpp to declare plugin metadata
// (mirrors Hub32's Q_PLUGIN_METADATA pattern, adapted for non-Qt)
#define HUB32API_PLUGIN_METADATA(UID, NAME, DESC, VER) \
    Uid         uid()         const override { return UID; }   \
    std::string name()        const override { return NAME; }  \
    std::string description() const override { return DESC; }  \
    std::string version()     const override { return VER; }

} // namespace hub32api
