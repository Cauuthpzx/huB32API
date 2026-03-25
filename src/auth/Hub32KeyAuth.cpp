#include "../core/PrecompiledHeader.hpp"
#include "Hub32KeyAuth.hpp"

namespace hub32api::auth {

Result<std::string> Hub32KeyAuth::authenticate(const Credentials& creds) const
{
    // TODO: Load Hub32 auth key from creds.keyData
    // TODO: Authenticate against Hub32 server using Hub32Connection + AuthenticationManager
    // TODO: On success, return subject string for JWT issuance
    spdlog::debug("[Hub32KeyAuth] authenticate stub keyName={}", creds.keyName);
    return Result<std::string>::fail(ApiError{
        ErrorCode::NotImplemented, "Hub32KeyAuth::authenticate not yet implemented"
    });
}

} // namespace hub32api::auth
