---
name: testing-pattern
description: Kích hoạt khi viết C++ unit tests, integration tests, hoặc test fixtures. Đảm bảo test coverage cho mọi module, dependency injection, descriptive names.
---

# Testing Pattern

## Quy tắc
- Mỗi module PHẢI có unit test.
- Test file: `tests/test_[module].cpp`
- Test name mô tả: `TEST(SchoolRepo, CreateSchoolReturnsValidId)`
- Dependency injection: inject mock/stub database, không dùng production DB.
- Test cả happy path VÀ error cases.
- Chạy `ctest` / `meson test` TRƯỚC mỗi commit.

## Template
```cpp
#include <gtest/gtest.h>
#include "db/SchoolRepository.hpp"
#include "test_helpers.hpp"  // in-memory SQLite setup

class SchoolRepoTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_db = createInMemoryDb();  // SQLite :memory:
        m_repo = std::make_unique<SchoolRepository>(m_db.get());
    }
    
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> m_db{nullptr, sqlite3_close};
    std::unique_ptr<SchoolRepository> m_repo;
};

TEST_F(SchoolRepoTest, CreateReturnsValidId) {
    auto result = m_repo->create("Test School", "123 Street");
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(result.value().id.empty());
    EXPECT_EQ(result.value().name, "Test School");
}

TEST_F(SchoolRepoTest, FindByIdReturnsNotFoundForMissingId) {
    auto result = m_repo->findById("nonexistent-id");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

TEST_F(SchoolRepoTest, DeleteRemovesSchool) {
    auto created = m_repo->create("To Delete", "");
    ASSERT_TRUE(created.is_ok());
    
    auto deleted = m_repo->remove(created.value().id);
    ASSERT_TRUE(deleted.is_ok());
    
    auto found = m_repo->findById(created.value().id);
    EXPECT_TRUE(found.is_err());
}
```

## KHÔNG LÀM
```cpp
// ❌ Test không có assertion
TEST(Foo, Bar) { foo.doSomething(); }  // verifies nothing

// ❌ Test phụ thuộc file system / network
TEST(Config, Load) { Config c("/etc/hub32/config.json"); }  // path might not exist

// ❌ Test phụ thuộc thứ tự
TEST(A, First) { globalState = 1; }
TEST(A, Second) { EXPECT_EQ(globalState, 1); }  // depends on First running first
```
