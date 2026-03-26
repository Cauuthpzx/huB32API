#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

namespace hub32api::core::internal {

/// @brief Cryptographic utilities using OpenSSL CSPRNG.
/// All methods return Result<T> instead of throwing on failure.
class HUB32API_EXPORT CryptoUtils
{
public:
    /// @brief Generates a RFC 4122 v4 UUID string.
    /// @return Result containing UUID string, or CryptoFailure error.
    static Result<std::string> generateUuid();

    /// @brief Generates cryptographically secure random bytes.
    /// @param count Number of random bytes to generate.
    /// @return Result containing byte vector, or CryptoFailure error.
    static Result<std::vector<uint8_t>> randomBytes(size_t count);

    CryptoUtils() = delete;
};

} // namespace hub32api::core::internal
