---
name: security-pattern
description: Kích hoạt khi viết auth, JWT, password hashing, input validation, hoặc bất kỳ code liên quan bảo mật. Đảm bảo RS256, argon2id, prepared statements, RAND_bytes.
---

# Security Pattern

## Quy tắc tuyệt đối
- JWT: RS256 ONLY. KHÔNG HS256. RSA 4096-bit keys.
- Password: argon2id (new) hoặc PBKDF2-SHA256 (legacy backward compat).
- Random: OpenSSL RAND_bytes. KHÔNG mt19937, rand(), random_device cho crypto.
- SQL: Prepared statements ONLY. KHÔNG string concatenation.
- Input: Validate TRƯỚC xử lý. Check length, format, type, bounds.
- TLS: Mandatory khi enabled. Server từ chối start nếu thiếu cert.
- Tokens: Persistent revocation (SQLite). KHÔNG in-memory fallback.
- Logging: KHÔNG log passwords, tokens, PII. Log action + subject + target.
- Config: Fail on critical error. KHÔNG warn và continue với defaults.

## Input validation template
```cpp
// Validate TRƯỚC mọi thứ khác
if (username.empty() || username.size() > kDefaultMaxFieldLength) {
    return Result<void>::fail(ApiError(ErrorCode::ValidationFailed, "Invalid username length"));
}
if (!validation_utils::is_valid_username(username)) {
    return Result<void>::fail(ApiError(ErrorCode::ValidationFailed, "Invalid username format"));
}

// safe_stoi thay std::stoi (no exception on invalid input)
auto pageSize = validation_utils::safe_stoi(req.get_param_value("limit"), kDefaultPageSize);
pageSize = std::clamp(pageSize, 1, kMaxPageSize);
```

## KHÔNG BAO GIỜ
```cpp
// ❌ HS256
jwt::create().set_algorithm("HS256");

// ❌ mt19937 cho crypto
std::mt19937 rng(std::random_device{}());

// ❌ String concat SQL
"WHERE name = '" + userInput + "'";

// ❌ Log sensitive data
spdlog::info("Login: user={} password={}", user, password);

// ❌ Hardcode credentials
if (username == "admin" && password == "admin123")
```
