Review all modified files in the current branch.

Check each file against these criteria:

1. Memory: smart pointers, RAII, no raw new/delete, no manual lock/unlock
2. Error handling: Result<T> returns, no swallowed catches, all sqlite3 codes checked
3. Threading: shared_mutex for read-heavy, atomic for counters, no volatile for sync
4. Constants: constexpr not #define, enum class not magic numbers, unit comments
5. Types: string_view for read-only params, optional for nullable, no raw owning pointers
6. Security: prepared statements, input validation first, no logged secrets
7. Naming: snake_case functions, PascalCase classes, kPascalCase constants, trailing_ members
8. Headers: #pragma once, include order (stdlib → third-party → project), forward declarations
9. Tests: every new function has test, descriptive test names, happy + error paths

For each issue:
- File:line
- Severity: ERROR / WARN
- Description + fix

After review: cmake --build --preset debug && ctest --preset debug
