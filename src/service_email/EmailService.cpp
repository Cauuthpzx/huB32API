#include "core/PrecompiledHeader.hpp"
#include "hub32api/service/EmailService.hpp"

#include <curl/curl.h>

#include <array>
#include <atomic>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

namespace hub32api::service {

// -----------------------------------------------------------------------
// Base64 helper — RFC 4648 standard alphabet, no line wrapping.
// Used for subject encoding (RFC 2047 B-encoding) and body encoding.
// -----------------------------------------------------------------------
namespace {

// SMTPS: implicit SSL/TLS on port 465 (no STARTTLS).
constexpr uint16_t kSmtpsPort = 465; // SMTPS: implicit SSL/TLS (không dùng STARTTLS)

// Monotonically increasing counter ensures boundary uniqueness even when two
// emails are built within the same clock second.
std::atomic<uint64_t> s_boundaryCounter{0};

constexpr std::array<char, 64> kBase64Table = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    'a','b','c','d','e','f','g','h','i','j','k','l','m',
    'n','o','p','q','r','s','t','u','v','w','x','y','z',
    '0','1','2','3','4','5','6','7','8','9','+','/'
};

std::string base64Encode(std::string_view input)
{
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    const auto* data = reinterpret_cast<const unsigned char*>(input.data());
    const std::size_t len = input.size();

    for (std::size_t i = 0; i < len; i += 3) {
        const unsigned int b0 = data[i];
        const unsigned int b1 = (i + 1 < len) ? data[i + 1] : 0u;
        const unsigned int b2 = (i + 2 < len) ? data[i + 2] : 0u;

        out += kBase64Table[(b0 >> 2) & 0x3F];
        out += kBase64Table[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
        out += (i + 1 < len) ? kBase64Table[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=';
        out += (i + 2 < len) ? kBase64Table[b2 & 0x3F] : '=';
    }
    return out;
}

// RFC 2047 B-encoding for UTF-8 subject.
std::string encodeSubject(std::string_view subject)
{
    return "=?UTF-8?B?" + base64Encode(subject) + "?=";
}

// RFC 2822 date string, e.g. "Mon, 27 Mar 2026 10:00:00 +0000"
// Thread-safe: uses localtime_s (Windows) / localtime_r (POSIX) instead of localtime().
std::string rfc2822Date()
{
    const std::time_t now = std::time(nullptr);
    struct tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &now);   // Windows thread-safe
#else
    localtime_r(&now, &tm_buf);   // POSIX thread-safe
#endif
    std::array<char, 64> buf{};
    std::strftime(buf.data(), buf.size(), "%a, %d %b %Y %H:%M:%S %z", &tm_buf);
    return std::string(buf.data());
}

// Build RFC 2822 email payload (plain-text only or multipart/alternative).
std::string buildEmailPayload(const EmailMessage& msg,
                              const std::string& fromAddress,
                              const std::string& fromName)
{
    std::ostringstream ss;

    const std::string fromHeader = fromName.empty()
        ? "<" + fromAddress + ">"
        : "\"" + fromName + "\" <" + fromAddress + ">";

    const std::string toHeader = msg.toName.empty()
        ? "<" + msg.to + ">"
        : "\"" + msg.toName + "\" <" + msg.to + ">";

    ss << "Date: "    << rfc2822Date()             << "\r\n"
       << "From: "   << fromHeader                 << "\r\n"
       << "To: "     << toHeader                   << "\r\n"
       << "Subject: " << encodeSubject(msg.subject) << "\r\n"
       << "MIME-Version: 1.0\r\n";

    if (msg.bodyHtml.empty()) {
        // Plain-text only
        ss << "Content-Type: text/plain; charset=UTF-8\r\n"
           << "Content-Transfer-Encoding: base64\r\n"
           << "\r\n"
           << base64Encode(msg.bodyText) << "\r\n";
    } else {
        // multipart/alternative with both plain and html parts
        const std::string boundary = "----=_Part_Hub32_"
            + std::to_string(static_cast<uint64_t>(std::time(nullptr)))
            + "_" + std::to_string(s_boundaryCounter.fetch_add(1, std::memory_order_relaxed));

        ss << "Content-Type: multipart/alternative; boundary=\"" << boundary << "\"\r\n"
           << "\r\n"
           << "--" << boundary << "\r\n"
           << "Content-Type: text/plain; charset=UTF-8\r\n"
           << "Content-Transfer-Encoding: base64\r\n"
           << "\r\n"
           << base64Encode(msg.bodyText) << "\r\n"
           << "--" << boundary << "\r\n"
           << "Content-Type: text/html; charset=UTF-8\r\n"
           << "Content-Transfer-Encoding: base64\r\n"
           << "\r\n"
           << base64Encode(msg.bodyHtml) << "\r\n"
           << "--" << boundary << "--\r\n";
    }

    return ss.str();
}

// libcurl read callback — reads bytes from a std::string via a size_t offset.
struct ReadState
{
    const std::string& data;
    std::size_t        offset = 0;
};

std::size_t curlReadCallback(char* buf, std::size_t size, std::size_t nmemb, void* userp)
{
    auto* state = static_cast<ReadState*>(userp);
    const std::size_t remaining = state->data.size() - state->offset;
    const std::size_t toCopy   = std::min(remaining, size * nmemb);
    if (toCopy == 0) return 0;
    std::memcpy(buf, state->data.data() + state->offset, toCopy);
    state->offset += toCopy;
    return toCopy;
}

// RAII wrapper for curl_easy handle.
struct CurlHandle
{
    CURL* handle = nullptr;

    CurlHandle() : handle(curl_easy_init()) {}
    ~CurlHandle()
    {
        if (handle) curl_easy_cleanup(handle);
    }

    CurlHandle(const CurlHandle&)            = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
};

// RAII wrapper for curl_slist.
struct CurlSlist
{
    curl_slist* list = nullptr;

    CurlSlist() = default;

    bool append(const std::string& s)
    {
        curl_slist* updated = curl_slist_append(list, s.c_str());
        if (!updated) {
            spdlog::error("[EmailService] curl_slist_append failed — out of memory");
            return false;
        }
        list = updated;
        return true;
    }

    ~CurlSlist()
    {
        if (list) curl_slist_free_all(list);
    }

    CurlSlist(const CurlSlist&)            = delete;
    CurlSlist& operator=(const CurlSlist&) = delete;
};

} // anonymous namespace

// -----------------------------------------------------------------------
// EmailService implementation
// -----------------------------------------------------------------------

EmailService::EmailService(Config cfg)
    : m_cfg(std::move(cfg))
{}

bool EmailService::isConfigured() const
{
    return !m_cfg.host.empty()
        && !m_cfg.username.empty()
        && !m_cfg.password.empty()
        && !m_cfg.fromAddress.empty();
}

Result<void> EmailService::send(const EmailMessage& msg) const
{
    if (!isConfigured()) {
        return Result<void>::fail(
            ApiError{ErrorCode::InvalidConfig, "SMTP not configured"});
    }

    CurlHandle curl;
    if (!curl.handle) {
        spdlog::error("[EmailService] curl_easy_init() failed — out of memory?");
        return Result<void>::fail(
            ApiError{ErrorCode::InternalError, "curl_easy_init() failed"});
    }

    // Choose protocol scheme based on port / useTls flag.
    // Port kSmtpsPort (465) uses implicit TLS (smtps://); all others use STARTTLS via smtp://.
    const std::string scheme = (m_cfg.port == kSmtpsPort) ? "smtps://" : "smtp://";
    const std::string url    = scheme + m_cfg.host + ":" + std::to_string(m_cfg.port);

    curl_easy_setopt(curl.handle, CURLOPT_URL, url.c_str());

    if (m_cfg.useTls && m_cfg.port != kSmtpsPort) {
        // STARTTLS
        curl_easy_setopt(curl.handle, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
    }

    curl_easy_setopt(curl.handle, CURLOPT_USERNAME, m_cfg.username.c_str());
    curl_easy_setopt(curl.handle, CURLOPT_PASSWORD, m_cfg.password.c_str());

    const std::string mailFrom = "<" + m_cfg.fromAddress + ">";
    curl_easy_setopt(curl.handle, CURLOPT_MAIL_FROM, mailFrom.c_str());

    CurlSlist rcptList;
    if (!rcptList.append("<" + msg.to + ">")) {
        return Result<void>::fail(ApiError{ErrorCode::InternalError,
                                           "failed to build SMTP recipient list"});
    }
    curl_easy_setopt(curl.handle, CURLOPT_MAIL_RCPT, rcptList.list);

    const std::string payload = buildEmailPayload(msg, m_cfg.fromAddress, m_cfg.fromName);
    ReadState readState{payload, 0};

    curl_easy_setopt(curl.handle, CURLOPT_READFUNCTION, curlReadCallback);
    curl_easy_setopt(curl.handle, CURLOPT_READDATA,     &readState);
    curl_easy_setopt(curl.handle, CURLOPT_UPLOAD,       1L);
    curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT,      static_cast<long>(m_cfg.timeoutSec));

    // SSL peer verification — controlled by Config::verifySsl.
    // true (default) = xác thực certificate SMTP server (bắt buộc production, ngăn MITM).
    // false = bỏ qua certificate (chỉ dùng test với SMTP server nội bộ không có cert hợp lệ).
    curl_easy_setopt(curl.handle, CURLOPT_SSL_VERIFYPEER, m_cfg.verifySsl ? 1L : 0L);
    curl_easy_setopt(curl.handle, CURLOPT_SSL_VERIFYHOST, m_cfg.verifySsl ? 2L : 0L);

    curl_easy_setopt(curl.handle, CURLOPT_VERBOSE, 0L);

    const CURLcode rc = curl_easy_perform(curl.handle);
    if (rc != CURLE_OK) {
        const std::string errMsg = std::string("curl SMTP error: ") + curl_easy_strerror(rc);
        spdlog::warn("[EmailService] {}", errMsg);
        return Result<void>::fail(ApiError{ErrorCode::InternalError, errMsg});
    }

    return Result<void>::ok();
}

} // namespace hub32api::service
