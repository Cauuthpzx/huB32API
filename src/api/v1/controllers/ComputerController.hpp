#pragma once

#include <memory>

namespace httplib { class Request; class Response; }
namespace hub32api::db { class ComputerRepository; }

namespace hub32api::api::v1 {

// -----------------------------------------------------------------------
// ComputerController — handles /api/v1/computers endpoints
//
// GET  /api/v1/computers          → list all computers
// GET  /api/v1/computers/:id      → get one computer
// GET  /api/v1/computers/:id/info → extended info (user + session + screens)
// -----------------------------------------------------------------------
class ComputerController
{
public:
    explicit ComputerController(db::ComputerRepository& repo);

    void handleList  (const httplib::Request& req, httplib::Response& res);
    void handleGetOne(const httplib::Request& req, httplib::Response& res);
    void handleInfo  (const httplib::Request& req, httplib::Response& res);

private:
    db::ComputerRepository& m_repo;
};

} // namespace hub32api::api::v1
