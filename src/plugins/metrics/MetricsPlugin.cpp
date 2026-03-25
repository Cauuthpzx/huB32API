#include "core/PrecompiledHeader.hpp"
#include "MetricsPlugin.hpp"
#include <sstream>

namespace hub32api::plugins {

void MetricsPlugin::recordRequest(int httpStatus)
{
    ++m_totalRequests;
    if (httpStatus >= 500) ++m_failedRequests;
}

void MetricsPlugin::recordConnection(bool connected)
{
    if (connected) ++m_activeConnections;
    else           --m_activeConnections;
}

int MetricsPlugin::totalRequests()     const noexcept { return m_totalRequests.load();     }
int MetricsPlugin::failedRequests()    const noexcept { return m_failedRequests.load();    }
int MetricsPlugin::activeConnections() const noexcept { return m_activeConnections.load(); }

std::string MetricsPlugin::prometheusText() const
{
    std::ostringstream oss;
    oss << "# HELP hub32api_requests_total Total HTTP requests\n"
        << "# TYPE hub32api_requests_total counter\n"
        << "hub32api_requests_total " << m_totalRequests.load() << "\n"
        << "# HELP hub32api_requests_failed_total Failed HTTP requests (5xx)\n"
        << "# TYPE hub32api_requests_failed_total counter\n"
        << "hub32api_requests_failed_total " << m_failedRequests.load() << "\n"
        << "# HELP hub32api_connections_active Active VNC connections\n"
        << "# TYPE hub32api_connections_active gauge\n"
        << "hub32api_connections_active " << m_activeConnections.load() << "\n";
    return oss.str();
}

} // namespace hub32api::plugins
