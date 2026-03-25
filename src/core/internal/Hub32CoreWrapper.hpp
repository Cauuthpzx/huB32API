#pragma once

#include <memory>
#include <string>
#include "hub32api/export.h"
#include "hub32api/config/ServerConfig.hpp"
#include "hub32api/core/Result.hpp"

// Forward declarations for Hub32 types (avoid pulling in Hub32Core.h directly)
class Hub32Core;
class QCoreApplication;

namespace hub32api::core::internal {

// -----------------------------------------------------------------------
// Hub32CoreWrapper — manages the Hub32Core Qt singleton lifetime.
//
// Hub32Core is a Qt-based singleton that requires QApplication/QCoreApplication
// to exist first. This wrapper owns both lifetimes and exposes only what
// the hub32api layer needs.
// -----------------------------------------------------------------------
class HUB32API_EXPORT Hub32CoreWrapper
{
public:
    explicit Hub32CoreWrapper(const ServerConfig& cfg);
    ~Hub32CoreWrapper();

    // Non-copyable, non-movable (singleton wrapper)
    Hub32CoreWrapper(const Hub32CoreWrapper&)            = delete;
    Hub32CoreWrapper& operator=(const Hub32CoreWrapper&) = delete;

    bool isInitialized() const noexcept;
    std::string hub32Version() const;
    std::string pluginDirectory() const;

    // Access underlying core (opaque pointer; only used by plugin bridges)
    Hub32Core* core() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hub32api::core::internal
