#pragma once

#include <mutex>
#include <unordered_set>
#include <string>

namespace veyon32api::auth::internal {

// -----------------------------------------------------------------------
// TokenStore — tracks revoked tokens (denylist).
// In-memory; flushed on restart. Sufficient for single-process deployment.
// -----------------------------------------------------------------------
class TokenStore
{
public:
    void revoke(const std::string& jti);
    bool isRevoked(const std::string& jti) const noexcept;
    void purgeExpired();

private:
    mutable std::mutex          m_mutex;
    std::unordered_set<std::string> m_revoked;
};

} // namespace veyon32api::auth::internal
