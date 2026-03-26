---
name: api-controller
description: Kích hoạt khi viết REST API controllers, endpoints, request/response handling. Đảm bảo validate input first, proper HTTP status, audit logging.
---

# API Controller Pattern

## Quy tắc
- VALIDATE INPUT FIRST. Trước mọi business logic.
- Return proper HTTP status codes (dùng http_status_for(ErrorCode)).
- JSON response format nhất quán.
- Audit log cho mọi mutation (create, update, delete).
- Role-based access check TRƯỚC business logic.
- DTO layer: controller nhận DTO, chuyển sang domain, trả DTO.

## Controller template
```cpp
void SchoolController::handleCreate(const httplib::Request& req, httplib::Response& res) {
    // 1. Auth check
    auto& auth = getAuthContext(req);
    if (!auth.is_admin()) {
        return sendError(res, ErrorCode::Unauthorized, "Admin role required");
    }
    
    // 2. Parse + validate input
    auto body = json::parse(req.body, nullptr, false);
    if (body.is_discarded()) {
        return sendError(res, ErrorCode::InvalidRequest, "Invalid JSON");
    }
    
    auto dto = body.get<dto::CreateSchoolRequest>();
    if (dto.name.empty() || dto.name.size() > kDefaultMaxFieldLength) {
        return sendError(res, ErrorCode::ValidationFailed, "Invalid school name");
    }
    
    // 3. Business logic
    auto result = m_schoolRepo.create(dto.name, dto.address);
    if (result.is_err()) {
        return sendError(res, result.error());
    }
    
    // 4. Audit log
    m_audit.log(auth.subject(), "create_school", "school", result.value().id);
    
    // 5. Response
    auto response = dto::SchoolResponse::from(result.value());
    res.status = 201;
    res.set_content(json(response).dump(), "application/json");
}
```

## Error response format
```json
{
  "error": "NotFound",
  "code": 404,
  "message": "School not found",
  "details": "No school with id 'abc-123'"
}
```

## KHÔNG LÀM
```cpp
// ❌ Business logic trước validation
auto school = repo.create(body["name"]);  // chưa validate!

// ❌ Return 200 cho mọi thứ
res.status = 200;  // delete thành công nên là 204

// ❌ Quên audit log cho mutations
repo.delete(id);  // ai xóa? khi nào? không biết
```
