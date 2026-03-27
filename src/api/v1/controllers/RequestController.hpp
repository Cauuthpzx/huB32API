#pragma once
#include <string>

namespace httplib { struct Request; struct Response; }
namespace hub32api::auth { class JwtAuth; }
namespace hub32api::db {
    class PendingRequestRepository;
    class ClassRepository;
    class StudentRepository;
    class TeacherRepository;
}

namespace hub32api::api::v1 {

/**
 * @brief REST controller for /api/v1/requests — the ticket/inbox system.
 *
 * Routing rules enforced here:
 *   - student submits → to_id = class.teacher_id  (or tenant_id if no teacher)
 *   - teacher submits → to_id = tenant_id  (owner inbox)
 *
 * Inbox (list pending):
 *   - teacher sees tickets addressed to their teacher_id
 *   - owner sees tickets addressed to their tenant_id
 *
 * Accept/reject:
 *   - only the direct recipient may accept or reject
 *   - on accept, password_hash from payload is applied to the correct table
 */
class RequestController
{
public:
    RequestController(db::PendingRequestRepository& requestRepo,
                      db::ClassRepository&          classRepo,
                      db::StudentRepository&        studentRepo,
                      db::TeacherRepository&        teacherRepo,
                      auth::JwtAuth&                jwtAuth);

    /** POST /api/v1/requests/change-password — submit a password change ticket */
    void handleSubmitChangePassword(const httplib::Request& req, httplib::Response& res);

    /** GET /api/v1/requests/inbox — list pending tickets addressed to the caller */
    void handleListInbox(const httplib::Request& req, httplib::Response& res);

    /** GET /api/v1/requests/outbox — list tickets submitted by the caller */
    void handleListOutbox(const httplib::Request& req, httplib::Response& res);

    /** POST /api/v1/requests/:id/accept — accept a pending ticket */
    void handleAccept(const httplib::Request& req, httplib::Response& res);

    /** POST /api/v1/requests/:id/reject — reject a pending ticket */
    void handleReject(const httplib::Request& req, httplib::Response& res);

private:
    db::PendingRequestRepository& m_requestRepo;
    db::ClassRepository&          m_classRepo;
    db::StudentRepository&        m_studentRepo;
    db::TeacherRepository&        m_teacherRepo;
    auth::JwtAuth&                m_jwtAuth;
};

} // namespace hub32api::api::v1
