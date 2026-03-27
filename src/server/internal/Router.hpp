#pragma once

#include <memory>
#include <string>

// Forward declarations
namespace httplib { class Server; }
namespace hub32api::core::internal { class PluginRegistry; class ConnectionPool; }
namespace hub32api::auth { class JwtAuth; class Hub32KeyAuth; class UserRoleStore; }
namespace hub32api::agent { class AgentRegistry; }
namespace hub32api::db { class SchoolRepository; class LocationRepository; class ComputerRepository;
                         class TeacherRepository; class TeacherLocationRepository;
                         class TenantRepository; class DatabaseManager; class StudentRepository;
                         class ClassRepository; class PendingRequestRepository; }
namespace hub32api::media { class SfuBackend; class RoomManager; }
namespace hub32api::service { class EmailService; }
namespace hub32api::api::v1::middleware { class RateLimitMiddleware; }
namespace hub32api::server { class SseManager; }

namespace hub32api::server::internal {

// -----------------------------------------------------------------------
// Router — registers all API routes on the httplib::Server instance.
// Single entry point: call registerAll() once at startup.
// -----------------------------------------------------------------------
class Router
{
public:
    struct Services
    {
        core::internal::PluginRegistry& registry;
        core::internal::ConnectionPool& pool;
        auth::JwtAuth&                  jwtAuth;
        auth::Hub32KeyAuth&             keyAuth;
        auth::UserRoleStore&            roleStore;
        agent::AgentRegistry&           agentRegistry;
        std::string                     agentKeyHash;  ///< PBKDF2-SHA256 hash of agent registration key
        db::SchoolRepository*           schoolRepo = nullptr;
        db::LocationRepository*         locationRepo = nullptr;
        db::ComputerRepository*         computerRepo = nullptr;
        db::TeacherRepository*          teacherRepo = nullptr;
        db::TeacherLocationRepository*  teacherLocationRepo = nullptr;
        db::StudentRepository*          studentRepo = nullptr;
        db::ClassRepository*            classRepo   = nullptr;
        media::RoomManager*             roomManager = nullptr;
        media::SfuBackend*              sfuBackend = nullptr;
        std::string                     turnSecret;
        std::string                     turnServerUrl;
        db::TenantRepository*           tenantRepo    = nullptr;
        db::DatabaseManager*            dbManager     = nullptr;
        db::PendingRequestRepository*   requestRepo   = nullptr;
        service::EmailService*          emailService  = nullptr;  // optional; nullptr = dev mode
        std::string                     appBaseUrl;               // base URL for verification links in emails
    };

    explicit Router(httplib::Server& server, Services svcs);

    void registerAll();

private:
    void registerV1();
    void registerV2();
    void registerHealthAndMetrics();
    void registerOpenApi();
    void registerAgentRoutes();
    void registerSchoolRoutes();
    void registerTeacherRoutes();
    void registerStudentRoutes();
    void registerClassRoutes();
    void registerStreamRoutes();
    void registerRegisterRoutes();
    void registerRequestRoutes();
    void registerSse();
    void registerDebug();

    httplib::Server& m_server;
    Services         m_svcs;
    std::shared_ptr<server::SseManager> m_sse;
    std::shared_ptr<api::v1::middleware::RateLimitMiddleware> m_rl;
};

} // namespace hub32api::server::internal
