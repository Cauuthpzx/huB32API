#pragma once

#include <atomic>
#include <string>
#include "veyon32api/plugins/PluginInterface.hpp"

namespace veyon32api::plugins {

// -----------------------------------------------------------------------
// MetricsPlugin — collects and exposes server metrics.
// Thread-safe counters incremented by HTTP layer; read by MetricsController.
// -----------------------------------------------------------------------
class MetricsPlugin final : public PluginInterface
{
public:
    VEYON32API_PLUGIN_METADATA(
        "a1b2c3d4-0004-0004-0004-000000000004",
        "MetricsPlugin",
        "Prometheus-compatible metrics collector",
        "1.0.0"
    )

    void recordRequest(int httpStatus);
    void recordConnection(bool connected);

    int  totalRequests()     const noexcept;
    int  failedRequests()    const noexcept;
    int  activeConnections() const noexcept;

    // Prometheus text exposition format
    std::string prometheusText() const;

private:
    std::atomic<int> m_totalRequests{0};
    std::atomic<int> m_failedRequests{0};
    std::atomic<int> m_activeConnections{0};
};

} // namespace veyon32api::plugins
