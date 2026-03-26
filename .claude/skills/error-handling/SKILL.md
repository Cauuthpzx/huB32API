---
name: error-handling
description: Kích hoạt khi viết error handling, Result<T>, SQLite return codes, hoặc API error responses. Đảm bảo Result<T> pattern, no silent failures.
---

# Error Handling [CCG E.6, E.25]

## Quy tắc
- `Result<T>` cho business logic. KHÔNG exceptions cho control flow.
- Exceptions CHỈ cho truly exceptional: out of memory, startup failure.
- MỌI system call: check return code. KHÔNG ignore.
- MỌI SQLite call: check SQLITE_OK / SQLITE_ROW / SQLITE_DONE.
- MỌI API endpoint: return proper HTTP status + error JSON.

## Result<T> pattern
```cpp
// Trả về Result, không throw
Result<School> SchoolRepository::findById(const std::string& id) {
    auto stmt = m_db.prepare("SELECT * FROM schools WHERE id = ?");
    stmt.bind(1, id);
    
    if (stmt.step() != SQLITE_ROW) {
        return Result<School>::fail(ApiError(ErrorCode::NotFound, "School not found"));
    }
    
    return Result<School>::ok(School{
        .id = stmt.column_text(0),
        .name = stmt.column_text(1),
    });
}

// Sử dụng
auto result = repo.findById(id);
if (result.is_err()) {
    return httpErrorResponse(result.error());
}
const auto& school = result.value();
```

## SQLite error checking
```cpp
// ✅ Check mọi return code
int rc = sqlite3_step(stmt);
if (rc != SQLITE_DONE) {
    spdlog::error("SQLite error: {}", sqlite3_errmsg(m_db));
    return Result<void>::fail(ApiError(ErrorCode::DatabaseError, sqlite3_errmsg(m_db)));
}

// ❌ KHÔNG: ignore return code
sqlite3_step(stmt);  // có thể fail mà không biết
```

## KHÔNG LÀM
```cpp
// ❌ Exception cho business logic
throw std::runtime_error("User not found");  // dùng Result::fail

// ❌ Silent catch
try { db.execute(sql); } catch(...) {}  // swallowed!

// ❌ Bool return (mất error info)
bool save(const User& u);  // dùng Result<void>
```
