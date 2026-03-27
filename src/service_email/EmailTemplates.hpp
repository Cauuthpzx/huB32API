#pragma once

#include <string>

#include "hub32api/export.h"
#include "hub32api/core/Result.hpp"
#include "hub32api/core/Error.hpp"
#include "hub32api/service/EmailService.hpp"

namespace hub32api::service {

/**
 * @brief Builds a registration-verification email for the given recipient.
 *
 * Validates that verifyUrl is a safe http/https URL. If invalid, returns a
 * text-only fallback email without a clickable link and logs a warning.
 *
 * @param toEmail   Recipient email address (must not be empty).
 * @param orgName   Organisation name (shown in greeting; HTML-escaped).
 * @param verifyUrl Full verification URL, e.g. "https://app.example.com/verify?token=xxx".
 * @return Result<EmailMessage> — ok with a populated EmailMessage, or fail on invalid input.
 */
[[nodiscard]] HUB32API_EXPORT Result<EmailMessage> makeVerificationEmail(
    const std::string& toEmail,
    const std::string& orgName,
    const std::string& verifyUrl);

} // namespace hub32api::service
