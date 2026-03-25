#include "../../core/PrecompiledHeader.hpp"
#include "TokenStore.hpp"

namespace hub32api::auth::internal {

void TokenStore::revoke(const std::string& jti)
{
    std::lock_guard lock(m_mutex);
    m_revoked.insert(jti);
}

bool TokenStore::isRevoked(const std::string& jti) const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_revoked.count(jti) > 0;
}

void TokenStore::purgeExpired()
{
    // TODO: store expiry times alongside JTIs and remove expired entries
    std::lock_guard lock(m_mutex);
    m_revoked.clear();
}

} // namespace hub32api::auth::internal
