Implement SQLite repository for entity $ARGUMENTS.

Follow sqlite-pattern skill exactly:
1. Create src/db/${ARGUMENTS}Repository.hpp and .cpp
2. Prepared statements for ALL queries
3. Result<T> returns, no exceptions
4. Check every sqlite3 return code
5. CRUD: create, findById, listAll, update, remove (minimum)
6. Add domain-specific queries as needed
7. Write tests in tests/test_${ARGUMENTS}_repo.cpp
8. Minimum 5 test cases: create, findById, listAll, update, remove + error case

Build and test after implementation.
