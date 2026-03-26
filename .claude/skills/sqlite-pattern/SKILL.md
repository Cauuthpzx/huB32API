---
name: sqlite-pattern
description: Kích hoạt khi viết SQLite queries, repositories, database operations. Đảm bảo prepared statements, WAL mode, proper error handling.
---

# SQLite Pattern

## Quy tắc
- Prepared statements cho MỌI query. KHÔNG string concatenation SQL.
- WAL mode bắt buộc: `PRAGMA journal_mode=WAL`
- Foreign keys bắt buộc: `PRAGMA foreign_keys=ON`
- Check MỌI sqlite3 return code.
- Transaction cho batch operations: BEGIN → operations → COMMIT/ROLLBACK.
- Repository pattern: 1 class per table, Result<T> returns.

## Repository template
```cpp
class SchoolRepository {
public:
    explicit SchoolRepository(sqlite3* db) : m_db(db) { prepareStatements(); }
    ~SchoolRepository() { finalizeStatements(); }

    Result<School> create(const std::string& name, const std::string& address) {
        std::string id = CryptoUtils::generateUuid();
        int64_t now = time_utils::now_unix();
        
        sqlite3_reset(m_insertStmt);
        sqlite3_bind_text(m_insertStmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(m_insertStmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(m_insertStmt, 3, address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(m_insertStmt, 4, now);
        
        int rc = sqlite3_step(m_insertStmt);
        if (rc != SQLITE_DONE) {
            return Result<School>::fail(ApiError(ErrorCode::InternalError, sqlite3_errmsg(m_db)));
        }
        return Result<School>::ok(School{id, name, address, now});
    }

private:
    void prepareStatements() {
        sqlite3_prepare_v2(m_db, "INSERT INTO schools(id,name,address,created_at) VALUES(?,?,?,?)", 
                          -1, &m_insertStmt, nullptr);
    }
    void finalizeStatements() {
        sqlite3_finalize(m_insertStmt);
    }
    
    sqlite3* m_db;
    sqlite3_stmt* m_insertStmt = nullptr;
};
```

## KHÔNG LÀM
```cpp
// ❌ String concatenation SQL (SQL injection!)
std::string sql = "SELECT * FROM users WHERE name = '" + name + "'";

// ❌ Ignore return codes
sqlite3_step(stmt);  // might fail

// ❌ Forget to reset prepared statement
sqlite3_bind_text(stmt, 1, ...);  // stale bindings from previous call
```
