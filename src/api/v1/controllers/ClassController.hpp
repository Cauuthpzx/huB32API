#pragma once
#include <string>

namespace httplib { struct Request; struct Response; }
namespace hub32api::auth { class JwtAuth; }
namespace hub32api::db { class ClassRepository; class TeacherRepository; }

namespace hub32api::api::v1 {

/**
 * @brief REST controller for /api/v1/classes endpoints.
 *
 * Owner role: full CRUD access to all classes in the tenant.
 * Teacher role: read-only access to their own classes (listByTeacher).
 */
class ClassController
{
public:
    ClassController(db::ClassRepository&   classRepo,
                    db::TeacherRepository& teacherRepo,
                    auth::JwtAuth&         jwtAuth);

    void handleCreate(const httplib::Request& req, httplib::Response& res);
    void handleList(const httplib::Request& req, httplib::Response& res);
    void handleGet(const httplib::Request& req, httplib::Response& res);
    void handleUpdate(const httplib::Request& req, httplib::Response& res);
    void handleDelete(const httplib::Request& req, httplib::Response& res);

private:
    db::ClassRepository&   m_classRepo;
    db::TeacherRepository& m_teacherRepo;
    auth::JwtAuth&         m_jwtAuth;
};

} // namespace hub32api::api::v1
