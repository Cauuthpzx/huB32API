#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "hub32api/export.h"

namespace hub32api::core::internal {

/// @note EXCEPTION POLICY: CryptoUtils is an internal utility that throws
/// std::runtime_error on CSPRNG failure (extremely rare — indicates broken
/// OpenSSL or OS entropy exhaustion). All PUBLIC API callers MUST catch
/// exceptions and convert to Result::fail().
class HUB32API_EXPORT CryptoUtils
{
public:
    static std::string generateUuid();
    static std::vector<uint8_t> randomBytes(size_t count);
    CryptoUtils() = delete;
};

} // namespace hub32api::core::internal
