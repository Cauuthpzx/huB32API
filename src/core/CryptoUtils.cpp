#include "PrecompiledHeader.hpp"
#include "internal/CryptoUtils.hpp"
#include "hub32api/core/Constants.hpp"
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>

namespace hub32api::core::internal {

Result<std::string> CryptoUtils::generateUuid()
{
    uint8_t bytes[kUuidBytes];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::CryptoFailure, "RAND_bytes failed for UUID generation"
        });
    }
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < kUuidBytes; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return Result<std::string>::ok(oss.str());
}

Result<std::vector<uint8_t>> CryptoUtils::randomBytes(size_t count)
{
    std::vector<uint8_t> buf(count);
    if (count > 0 && RAND_bytes(buf.data(), static_cast<int>(count)) != 1) {
        return Result<std::vector<uint8_t>>::fail(ApiError{
            ErrorCode::CryptoFailure, "RAND_bytes failed"
        });
    }
    return Result<std::vector<uint8_t>>::ok(std::move(buf));
}

} // namespace hub32api::core::internal
