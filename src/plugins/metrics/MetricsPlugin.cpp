#include "core/PrecompiledHeader.hpp"
#include "MetricsPlugin.hpp"
#include <sstream>

namespace veyon32api::plugins {

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
    oss << "# HELP veyon32api_requests_total Total HTTP requests\n"
        << "# TYPE veyon32api_requests_total counter\n"
        << "veyon32api_requests_total " << m_totalRequests.load() << "\n"
        << "# HELP veyon32api_requests_failed_total Failed HTTP requests (5xx)\n"
        << "# TYPE veyon32api_requests_failed_total counter\n"
        << "veyon32api_requests_failed_total " << m_failedRequests.load() << "\n"
        << "# HELP veyon32api_connections_active Active VNC connections\n"
        << "# TYPE veyon32api_connections_active gauge\n"
        << "veyon32api_connections_active " << m_activeConnections.load() << "\n";
    return oss.str();
}

} // namespace veyon32api::plugins
