#include "PrecompiledHeader.hpp"
#include "internal/CryptoUtils.hpp"
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace hub32api::core::internal {

std::string CryptoUtils::generateUuid()
{
    uint8_t bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("RAND_bytes failed for UUID generation");
    }
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return oss.str();
}

std::vector<uint8_t> CryptoUtils::randomBytes(size_t count)
{
    std::vector<uint8_t> buf(count);
    if (count > 0 && RAND_bytes(buf.data(), static_cast<int>(count)) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return buf;
}

} // namespace hub32api::core::internal
