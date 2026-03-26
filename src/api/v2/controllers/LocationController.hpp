#pragma once

namespace httplib { class Request; class Response; }
namespace hub32api::db { class LocationRepository; class ComputerRepository; }

namespace hub32api::api::v2 {

// GET /api/v2/locations       → LocationListDto
// GET /api/v2/locations/:id   → LocationDto with computerIds
class LocationController
{
public:
    LocationController(db::LocationRepository& locationRepo,
                       db::ComputerRepository& computerRepo);

    void handleList  (const httplib::Request& req, httplib::Response& res);
    void handleGetOne(const httplib::Request& req, httplib::Response& res);

private:
    db::LocationRepository&  m_locationRepo;
    db::ComputerRepository&  m_computerRepo;
};

} // namespace hub32api::api::v2
