#pragma once

#include <memory>
#include "hub32api/core/Result.hpp"

// Forward declarations
namespace httplib { class Request; class Response; }
namespace hub32api::auth { class JwtAuth; class Hub32KeyAuth; class UserRoleStore; }
namespace hub32api::db { class TeacherRepository; class StudentRepository; }

namespace hub32api::api::v1 {

// -----------------------------------------------------------------------
// AuthController — handles POST /api/v1/auth and DELETE /api/v1/auth (logout)
// -----------------------------------------------------------------------
class AuthController
{
public:
    explicit AuthController(
        auth::JwtAuth&          jwtAuth,
        auth::Hub32KeyAuth&     keyAuth,
        auth::UserRoleStore&    roleStore,
        db::TeacherRepository&  teacherRepo,
        db::StudentRepository&  studentRepo);

    // POST /api/v1/auth
    void handleLogin(const httplib::Request& req, httplib::Response& res);

    // DELETE /api/v1/auth
    void handleLogout(const httplib::Request& req, httplib::Response& res);

private:
    auth::JwtAuth&         m_jwtAuth;
    auth::Hub32KeyAuth&    m_keyAuth;
    auth::UserRoleStore&   m_roleStore;
    db::TeacherRepository& m_teacherRepo;
    db::StudentRepository& m_studentRepo;
};

} // namespace hub32api::api::v1
