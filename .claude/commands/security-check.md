Review all modified files for security issues.

Check for:
1. JWT: any HS256 reference, hardcoded secrets, missing RS256 validation
2. SQL injection: any string concatenation in SQL queries (must use prepared statements)
3. Input validation: unchecked user input, missing length/bounds checks, raw std::stoi
4. Password: plaintext storage, weak hashing, missing salt
5. Crypto: mt19937/rand() used for crypto (must use RAND_bytes)
6. Error handling: swallowed catches, unchecked sqlite3 return codes
7. Logging: passwords, tokens, or PII in log messages
8. Config: silent fallback to defaults on critical errors
9. Memory: raw new/delete, manual lock/unlock, missing RAII

For each issue found, report:
- File path + line number
- Severity: CRITICAL / HIGH / MEDIUM / LOW
- Description
- Fix

After review, run: cmake --build --preset debug && ctest --preset debug
