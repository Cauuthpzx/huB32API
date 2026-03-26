#pragma once

#include <string>
#include <mutex>
#include <unordered_map>
#include "hub32api/export.h"
#include "hub32api/core/Result.hpp"

namespace hub32api::auth {

/**
 * @brief File-backed user/role store for authentication and authorization.
 *
 * SECURITY: This replaces the hardcoded role assignment that allowed
 * any request with username="admin" to receive admin privileges.
 * Role is now determined by lookup in a persistent store after
 * password verification -- no code path derives role from username.
 *
 * ATTACK PREVENTED: Privilege escalation via username string match.
 * POST /api/v1/auth {"method":"logon","username":"admin"} no longer
 * grants admin role. The attacker must know the correct password AND
 * the username must exist in the store with admin role assigned.
 *
 * File format (users.json):
 * {
 *   "users": [
 *     {
 *       "username": "admin",
 *       "passwordHash": "$pbkdf2-sha256$100000$<salt_hex>$<hash_hex>",
 *       "role": "admin"
 *     }
 *   ]
 * }
 *
 * Bootstrap:
 *   To create the first admin user, create users.json with a hashed password.
 *   Use: hub32api-service --hash-password <password> to generate the hash.
 *   Or create programmatically using hashPassword() static method.
 */
class HUB32API_EXPORT UserRoleStore
{
public:
    /**
     * @brief Constructs the store and loads users from a JSON file.
     * @param filePath Path to users.json. If empty or missing, store is empty (fail closed).
     */
    explicit UserRoleStore(const std::string& filePath = {});

    /**
     * @brief Authenticates a user and returns their role.
     * @param username The username to look up.
     * @param password The plaintext password to verify.
     * @return Result containing the role string on success, or error on auth failure.
     */
    Result<std::string> authenticate(const std::string& username, const std::string& password) const;

    /**
     * @brief Checks if any users are loaded (store is functional).
     */
    bool hasUsers() const;

    /**
     * @brief Hashes a password using PBKDF2-HMAC-SHA256.
     * @param password Plaintext password.
     * @return Encoded hash string: $pbkdf2-sha256$iterations$salt_hex$hash_hex
     */
    static Result<std::string> hashPassword(const std::string& password);

    /**
     * @brief Verifies a password against a stored hash.
     * @param password Plaintext password to check.
     * @param storedHash The hash string from the store.
     * @return true if the password matches.
     */
    static bool verifyPassword(const std::string& password, const std::string& storedHash);

private:
    struct UserEntry {
        std::string username;
        std::string passwordHash;
        std::string role;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, UserEntry> m_users;
    std::string m_filePath;

    void loadFromFile(const std::string& path);
};

} // namespace hub32api::auth
