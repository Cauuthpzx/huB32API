/**
 * @file UserRoleStore.cpp
 * @brief Implementation of file-backed user/role store with PBKDF2-SHA256 password hashing.
 *
 * SECURITY FIX: Removes hardcoded admin role escalation vulnerability.
 *
 * ATTACK SCENARIO (fixed):
 *   Attacker sends POST /api/v1/auth with body {"method":"logon","username":"admin"}.
 *   Server compares username string to "admin", grants admin role with zero
 *   credential verification. Attacker gets full admin JWT on first request.
 *   Blast radius: complete system compromise.
 *
 * FIX: Role is now determined by authenticated lookup in this persistent store.
 * Password must match the PBKDF2-SHA256 hash stored in users.json, and role
 * comes from the store entry, not from the username string. If users.json is
 * missing or corrupt, all login attempts fail (fail closed).
 */

#include "../core/PrecompiledHeader.hpp"
#include "UserRoleStore.hpp"
#include "../utils/string_utils.hpp"

#include <fstream>
#include <sstream>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>  // for CRYPTO_memcmp

namespace hub32api::auth {

UserRoleStore::UserRoleStore(const std::string& filePath)
    : m_filePath(filePath)
{
    if (!filePath.empty()) {
        loadFromFile(filePath);
    }

    if (m_users.empty()) {
        spdlog::warn("[UserRoleStore] no users loaded — all login attempts will be rejected. "
                     "Create a users.json file to enable authentication. "
                     "Use --hash-password to generate password hashes.");
    } else {
        spdlog::info("[UserRoleStore] loaded {} user(s) from {}", m_users.size(), filePath);
    }
}

void UserRoleStore::loadFromFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        spdlog::warn("[UserRoleStore] cannot open users file: {}", path);
        return;
    }

    try {
        const auto j = nlohmann::json::parse(ifs);
        if (!j.contains("users") || !j["users"].is_array()) {
            spdlog::error("[UserRoleStore] users.json must contain a 'users' array");
            return;
        }

        for (const auto& u : j["users"]) {
            UserEntry entry;
            entry.username     = u.value("username", "");
            entry.passwordHash = u.value("passwordHash", "");
            entry.role         = u.value("role", "teacher");

            if (entry.username.empty() || entry.passwordHash.empty()) {
                spdlog::warn("[UserRoleStore] skipping user with empty username or passwordHash");
                continue;
            }

            m_users[entry.username] = std::move(entry);
        }
    }
    catch (const std::exception& ex) {
        spdlog::error("[UserRoleStore] failed to parse {}: {}", path, ex.what());
    }
}

Result<std::string> UserRoleStore::authenticate(const std::string& username, const std::string& password) const
{
    std::lock_guard lock(m_mutex);

    auto it = m_users.find(username);
    if (it == m_users.end()) {
        // SECURITY: Don't reveal whether the username exists or not.
        // Use the same error message for "user not found" and "wrong password".
        return Result<std::string>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Invalid username or password"
        });
    }

    if (!verifyPassword(password, it->second.passwordHash)) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Invalid username or password"
        });
    }

    return Result<std::string>::ok(std::string(it->second.role));
}

bool UserRoleStore::hasUsers() const
{
    std::lock_guard lock(m_mutex);
    return !m_users.empty();
}

Result<std::string> UserRoleStore::hashPassword(const std::string& password)
{
    unsigned char salt[kPbkdf2SaltBytes];
    if (RAND_bytes(salt, kPbkdf2SaltBytes) != 1) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::CryptoFailure, "RAND_bytes failed for password salt"
        });
    }

    unsigned char hash[kPbkdf2HashBytes];
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt, kPbkdf2SaltBytes,
            kPbkdf2Iterations,
            EVP_sha256(),
            kPbkdf2HashBytes, hash) != 1) {
        return Result<std::string>::fail(ApiError{
            ErrorCode::CryptoFailure, "PBKDF2_HMAC failed"
        });
    }

    return Result<std::string>::ok(
        "$pbkdf2-sha256$" + std::to_string(kPbkdf2Iterations) + "$" +
        utils::bytes_to_hex(salt, kPbkdf2SaltBytes) + "$" +
        utils::bytes_to_hex(hash, kPbkdf2HashBytes));
}

bool UserRoleStore::verifyPassword(const std::string& password, const std::string& storedHash)
{
    // Parse: $pbkdf2-sha256$iterations$salt_hex$hash_hex
    if (storedHash.rfind("$pbkdf2-sha256$", 0) != 0) return false;

    // Split by '$' -- fields: [empty, "pbkdf2-sha256", iterations, salt, hash]
    std::vector<std::string> parts;
    std::istringstream ss(storedHash);
    std::string part;
    while (std::getline(ss, part, '$')) {
        parts.push_back(part);
    }

    if (parts.size() != 5) return false;
    // parts[0] = "" (before first $)
    // parts[1] = "pbkdf2-sha256"
    // parts[2] = iterations
    // parts[3] = salt_hex
    // parts[4] = hash_hex

    int iterations;
    try { iterations = std::stoi(parts[2]); }
    catch (...) { return false; }

    auto salt = utils::hex_to_bytes(parts[3]);
    auto expectedHash = utils::hex_to_bytes(parts[4]);

    if (salt.empty() || expectedHash.empty()) return false;

    unsigned char computed[kPbkdf2HashBytes];
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt.data(), static_cast<int>(salt.size()),
            iterations,
            EVP_sha256(),
            kPbkdf2HashBytes, computed) != 1) {
        return false;
    }

    // SECURITY: Constant-time comparison to prevent timing attacks
    return CRYPTO_memcmp(computed, expectedHash.data(),
                         std::min<size_t>(kPbkdf2HashBytes, expectedHash.size())) == 0;
}

} // namespace hub32api::auth
