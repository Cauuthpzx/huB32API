#pragma once
#include <string>

namespace httplib { struct Request; struct Response; }
namespace hub32api::auth { class JwtAuth; }
namespace hub32api::db { class StudentRepository; }

namespace hub32api::api::v1 {

/**
 * @brief REST controller for /api/v1/students endpoints.
 *
 * All CRUD methods require teacher or owner role (enforced inside each handler).
 * handleActivateMachine is the only student-facing endpoint — it accepts a student
 * JWT and binds a machine fingerprint to the student account (one-time activation).
 */
class StudentController
{
public:
    StudentController(db::StudentRepository& studentRepo,
                      auth::JwtAuth& jwtAuth);

    // teacher/owner endpoints
    void handleCreate(const httplib::Request& req, httplib::Response& res);
    void handleList(const httplib::Request& req, httplib::Response& res);
    void handleGet(const httplib::Request& req, httplib::Response& res);
    void handleUpdate(const httplib::Request& req, httplib::Response& res);
    void handleDelete(const httplib::Request& req, httplib::Response& res);
    void handleResetMachine(const httplib::Request& req, httplib::Response& res);

    // student self-service endpoint — accepts student Bearer token (no teacher role required)
    void handleActivateMachine(const httplib::Request& req, httplib::Response& res);

private:
    db::StudentRepository& m_studentRepo;
    auth::JwtAuth&         m_jwtAuth;
};

} // namespace hub32api::api::v1
