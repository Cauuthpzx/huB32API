#include <gtest/gtest.h>
#include "core/internal/CryptoUtils.hpp"
#include <set>
#include <regex>

using hub32api::core::internal::CryptoUtils;

TEST(CryptoUtils, GenerateUuid_FormatValid)
{
    const auto uuid = CryptoUtils::generateUuid();
    std::regex uuidRegex(
        "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    EXPECT_TRUE(std::regex_match(uuid, uuidRegex))
        << "UUID does not match v4 format: " << uuid;
}

TEST(CryptoUtils, GenerateUuid_Unique1000)
{
    std::set<std::string> uuids;
    for (int i = 0; i < 1000; ++i) {
        uuids.insert(CryptoUtils::generateUuid());
    }
    EXPECT_EQ(uuids.size(), 1000u) << "UUID collision in 1000 generations";
}

TEST(CryptoUtils, GenerateRandomBytes_Length)
{
    auto bytes = CryptoUtils::randomBytes(32);
    EXPECT_EQ(bytes.size(), 32u);
}

TEST(CryptoUtils, GenerateRandomBytes_NotAllZero)
{
    auto bytes = CryptoUtils::randomBytes(32);
    bool allZero = true;
    for (auto b : bytes) { if (b != 0) { allZero = false; break; } }
    EXPECT_FALSE(allZero);
}
