#pragma once
#include <string>

namespace httplib { struct Request; struct Response; }
namespace hub32api::auth { class JwtAuth; }
namespace hub32api::db { class SchoolRepository; class LocationRepository; class ComputerRepository; }

namespace hub32api::api::v1 {

class SchoolController
{
public:
    SchoolController(db::SchoolRepository& schoolRepo,
                     db::LocationRepository& locationRepo,
                     db::ComputerRepository& computerRepo,
                     auth::JwtAuth& jwtAuth);

    void handleCreateSchool(const httplib::Request& req, httplib::Response& res);
    void handleListSchools(const httplib::Request& req, httplib::Response& res);
    void handleGetSchool(const httplib::Request& req, httplib::Response& res);
    void handleUpdateSchool(const httplib::Request& req, httplib::Response& res);
    void handleDeleteSchool(const httplib::Request& req, httplib::Response& res);

    void handleCreateLocation(const httplib::Request& req, httplib::Response& res);
    void handleListLocations(const httplib::Request& req, httplib::Response& res);
    void handleGetLocation(const httplib::Request& req, httplib::Response& res);
    void handleUpdateLocation(const httplib::Request& req, httplib::Response& res);
    void handleDeleteLocation(const httplib::Request& req, httplib::Response& res);
    void handleListLocationComputers(const httplib::Request& req, httplib::Response& res);

private:
    db::SchoolRepository& m_schoolRepo;
    db::LocationRepository& m_locationRepo;
    db::ComputerRepository& m_computerRepo;
    auth::JwtAuth& m_jwtAuth;
};

} // namespace hub32api::api::v1
