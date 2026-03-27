#pragma once

#include <string>
#include <cstdint>

#include "hub32api/export.h"
#include "hub32api/core/Result.hpp"

namespace hub32api::service {

// -----------------------------------------------------------------------
// EmailMessage — data for a single outgoing email.
// -----------------------------------------------------------------------
struct EmailMessage
{
    std::string to;        // recipient address
    std::string toName;    // recipient display name (may be empty)
    std::string subject;   // email subject (UTF-8)
    std::string bodyText;  // plain-text body (UTF-8)
    std::string bodyHtml;  // HTML body (UTF-8); empty = plain-text only
};

// -----------------------------------------------------------------------
// EmailService — sends SMTP email via libcurl.
//
// Thread safety: send() is const and does not mutate shared state;
// safe to call concurrently from multiple threads.
// -----------------------------------------------------------------------
class HUB32API_EXPORT EmailService
{
public:
    // Config là plain data aggregate (POD-like) — members không có trailing underscore
    // theo quy ước project cho data structs dùng làm initializer.
    struct Config
    {
        std::string host;
        uint16_t    port         = 587;   // 587 = STARTTLS, 465 = SMTPS
        bool        useTls       = true;
        std::string username;
        std::string password;             // loaded from file by caller; never hardcoded
        std::string fromAddress;
        std::string fromName;
        int         timeoutSec   = 10;   // seconds
        bool        verifySsl    = true; // true = xác thực certificate SMTP server (bắt buộc production)
                                         // false = bỏ qua certificate (chỉ dùng khi test với server nội bộ)
    };

    explicit EmailService(Config cfg);

    // Sends one email.  Returns ok() on success, fail() with a message on error.
    [[nodiscard]] Result<void> send(const EmailMessage& msg) const;

    // Returns true when host, username, password, and fromAddress are all set.
    [[nodiscard]] bool isConfigured() const;

private:
    Config m_cfg;
};

} // namespace hub32api::service
