#include "../core/PrecompiledHeader.hpp"
#include "VeyonKeyAuth.hpp"

namespace veyon32api::auth {

Result<std::string> VeyonKeyAuth::authenticate(const Credentials& creds) const
{
    // TODO: Load Veyon auth key from creds.keyData
    // TODO: Authenticate against Veyon server using VeyonConnection + AuthenticationManager
    // TODO: On success, return subject string for JWT issuance
    spdlog::debug("[VeyonKeyAuth] authenticate stub keyName={}", creds.keyName);
    return Result<std::string>::fail(ApiError{
        ErrorCode::NotImplemented, "VeyonKeyAuth::authenticate not yet implemented"
    });
}

} // namespace veyon32api::auth
