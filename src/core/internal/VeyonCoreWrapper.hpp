#pragma once

#include <memory>
#include <string>
#include "veyon32api/export.h"
#include "veyon32api/config/ServerConfig.hpp"
#include "veyon32api/core/Result.hpp"

// Forward declarations for Veyon types (avoid pulling in VeyonCore.h directly)
class VeyonCore;
class QCoreApplication;

namespace veyon32api::core::internal {

// -----------------------------------------------------------------------
// VeyonCoreWrapper — manages the VeyonCore Qt singleton lifetime.
//
// VeyonCore is a Qt-based singleton that requires QApplication/QCoreApplication
// to exist first. This wrapper owns both lifetimes and exposes only what
// the veyon32api layer needs.
// -----------------------------------------------------------------------
class VEYON32API_EXPORT VeyonCoreWrapper
{
public:
    explicit VeyonCoreWrapper(const ServerConfig& cfg);
    ~VeyonCoreWrapper();

    // Non-copyable, non-movable (singleton wrapper)
    VeyonCoreWrapper(const VeyonCoreWrapper&)            = delete;
    VeyonCoreWrapper& operator=(const VeyonCoreWrapper&) = delete;

    bool isInitialized() const noexcept;
    std::string veyonVersion() const;
    std::string pluginDirectory() const;

    // Access underlying core (opaque pointer; only used by plugin bridges)
    VeyonCore* core() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace veyon32api::core::internal
