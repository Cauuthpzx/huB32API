#pragma once
#include <string>

namespace httplib { struct Request; struct Response; }
namespace hub32api::auth { class JwtAuth; }
namespace hub32api::db { class TeacherRepository; class TeacherLocationRepository; class LocationRepository; }

namespace hub32api::api::v1 {

class TeacherController
{
public:
    TeacherController(db::TeacherRepository& teacherRepo,
                      db::TeacherLocationRepository& teacherLocationRepo,
                      db::LocationRepository& locationRepo,
                      auth::JwtAuth& jwtAuth);

    void handleCreateTeacher(const httplib::Request& req, httplib::Response& res);
    void handleListTeachers(const httplib::Request& req, httplib::Response& res);
    void handleGetTeacher(const httplib::Request& req, httplib::Response& res);
    void handleUpdateTeacher(const httplib::Request& req, httplib::Response& res);
    void handleDeleteTeacher(const httplib::Request& req, httplib::Response& res);

    void handleAssignLocation(const httplib::Request& req, httplib::Response& res);
    void handleRevokeLocation(const httplib::Request& req, httplib::Response& res);

private:
    db::TeacherRepository& m_teacherRepo;
    db::TeacherLocationRepository& m_teacherLocationRepo;
    db::LocationRepository& m_locationRepo;
    auth::JwtAuth& m_jwtAuth;
};

} // namespace hub32api::api::v1
