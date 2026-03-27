#include "core/PrecompiledHeader.hpp"
#include "EmailTemplates.hpp"

#include <array>
#include <ctime>
#include <sstream>

namespace hub32api::service {

namespace {

// Subject constant — avoids magic string in implementation.
constexpr std::string_view kVerificationSubject = "Xác nhận đăng ký tài khoản Hub32";

// Escapes special HTML characters to prevent XSS injection.
std::string htmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

// Returns true only for http:// or https:// URLs to prevent javascript:/data: injection.
bool isSafeHttpsUrl(const std::string& url)
{
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

// Returns the current 4-digit year as a string (thread-safe).
std::string currentYear()
{
    const std::time_t now = std::time(nullptr);
    struct tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    std::array<char, 8> buf{};
    std::strftime(buf.data(), buf.size(), "%Y", &tm_buf);
    return std::string(buf.data());
}

} // anonymous namespace

Result<EmailMessage> makeVerificationEmail(const std::string& toEmail,
                                           const std::string& orgName,
                                           const std::string& verifyUrl)
{
    // ── Input validation ────────────────────────────────────────────────────
    if (toEmail.empty()) {
        return Result<EmailMessage>::fail(
            ApiError{ErrorCode::InvalidRequest, "makeVerificationEmail: toEmail is empty"});
    }
    if (!isSafeHttpsUrl(verifyUrl)) {
        spdlog::warn("[EmailTemplates] unsafe or empty verifyUrl — sending text-only fallback");
        EmailMessage fallback;
        fallback.to      = toEmail;
        fallback.subject = std::string(kVerificationSubject);
        std::ostringstream ss;
        ss << "Chào mừng đến với Hub32!\r\n\r\n"
           << "Cảm ơn bạn đã đăng ký tổ chức \"" << orgName << "\".\r\n\r\n"
           << "Để kích hoạt tài khoản, vui lòng liên hệ quản trị viên để nhận liên kết xác nhận.\r\n\r\n"
           << "Trân trọng,\r\nĐội ngũ Hub32";
        fallback.bodyText = ss.str();
        return Result<EmailMessage>::ok(std::move(fallback));
    }

    const std::string safeOrgName  = htmlEscape(orgName);
    const std::string safeUrl      = htmlEscape(verifyUrl);
    const std::string year         = currentYear();

    EmailMessage msg;
    msg.to      = toEmail;
    msg.subject = std::string(kVerificationSubject);

    // ── Plain-text fallback ──────────────────────────────────────────────
    {
        std::ostringstream ss;
        ss << "Chào mừng đến với Hub32!\r\n\r\n"
           << "Cảm ơn bạn đã đăng ký tổ chức \"" << orgName << "\".\r\n\r\n"
           << "Để kích hoạt tài khoản, vui lòng truy cập liên kết sau:\r\n"
           << verifyUrl << "\r\n\r\n"
           << "Liên kết có hiệu lực trong 24 giờ.\r\n\r\n"
           << "Nếu bạn không thực hiện đăng ký này, vui lòng bỏ qua email này.\r\n\r\n"
           << "Trân trọng,\r\nĐội ngũ Hub32";
        msg.bodyText = ss.str();
    }

    // ── HTML body ────────────────────────────────────────────────────────
    {
        std::ostringstream ss;
        ss <<
R"html(<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <meta http-equiv="X-UA-Compatible" content="IE=edge" />
  <title>Xac nhan dang ky Hub32</title>
  <style>
    @media only screen and (max-width:620px){
      .wrapper{width:100%!important;padding:16px!important}
      .btn{width:100%!important;display:block!important;text-align:center!important}
    }
  </style>
</head>
<body style="margin:0;padding:0;background-color:#f0f2f5;-webkit-font-smoothing:antialiased">
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0">
  <tr>
    <td align="center" style="padding:40px 16px">

      <!-- Card -->
      <table role="presentation" class="wrapper" width="560" cellpadding="0" cellspacing="0" border="0"
             style="background:#ffffff;border-radius:12px;overflow:hidden;
                    box-shadow:0 1px 3px rgba(0,0,0,.1),0 8px 24px rgba(0,0,0,.06)">

        <!-- Top accent bar -->
        <tr>
          <td style="background:linear-gradient(135deg,#1d4ed8 0%,#2563eb 100%);height:4px;line-height:4px;font-size:4px">&nbsp;</td>
        </tr>

        <!-- Logo / Brand -->
        <tr>
          <td align="center" style="padding:36px 48px 28px">
            <table role="presentation" cellpadding="0" cellspacing="0" border="0">
              <tr>
                <td style="background:#1d4ed8;border-radius:10px;width:44px;height:44px;text-align:center;vertical-align:middle">
                  <span style="color:#ffffff;font-size:22px;font-weight:800;font-family:Arial,sans-serif;line-height:44px">H</span>
                </td>
                <td style="padding-left:12px;vertical-align:middle">
                  <span style="font-size:22px;font-weight:700;color:#0f172a;font-family:Arial,sans-serif;letter-spacing:-0.3px">Hub32</span>
                </td>
              </tr>
            </table>
          </td>
        </tr>

        <!-- Main content -->
        <tr>
          <td style="padding:0 48px 40px">
            <h1 style="margin:0 0 8px;font-size:22px;font-weight:700;color:#0f172a;
                        font-family:Arial,sans-serif;letter-spacing:-0.3px">
              Xac nhan dia chi email
            </h1>
            <p style="margin:0 0 20px;font-size:15px;color:#64748b;line-height:1.65;font-family:Arial,sans-serif">
              Xin chao,<br/>
              Cam on ban da dang ky to chuc
              <strong style="color:#0f172a">)html"
        << safeOrgName <<
R"html(</strong> tren Hub32.<br/>
              Vui long xac nhan dia chi email de kich hoat tai khoan.
            </p>

            <!-- CTA Button -->
            <table role="presentation" cellpadding="0" cellspacing="0" border="0" style="margin:28px 0">
              <tr>
                <td style="border-radius:8px;background:#1d4ed8">
                  <a href=")html"
        << safeUrl <<
R"html(" class="btn"
                     style="display:inline-block;padding:14px 32px;font-size:15px;font-weight:600;
                            color:#ffffff;text-decoration:none;border-radius:8px;
                            font-family:Arial,sans-serif;letter-spacing:0.1px;
                            background:#1d4ed8;border:1px solid #1d4ed8">
                    &#10003;&nbsp; Xac nhan tai khoan
                  </a>
                </td>
              </tr>
            </table>

            <!-- Divider -->
            <table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0">
              <tr>
                <td style="border-top:1px solid #e2e8f0;padding-top:24px">
                  <p style="margin:0 0 6px;font-size:12px;color:#94a3b8;font-family:Arial,sans-serif;
                             text-transform:uppercase;letter-spacing:0.6px;font-weight:600">
                    Hoac sao chep lien ket nay vao trinh duyet
                  </p>
                  <p style="margin:0;font-size:12px;font-family:'Courier New',monospace;
                             color:#475569;word-break:break-all;line-height:1.5;
                             background:#f8fafc;border:1px solid #e2e8f0;
                             border-radius:6px;padding:10px 12px">
                    <a href=")html"
        << safeUrl <<
R"html(" style="color:#1d4ed8;text-decoration:none">)html"
        << safeUrl <<
R"html(</a>
                  </p>
                </td>
              </tr>
            </table>

            <!-- Warning note -->
            <table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0" style="margin-top:24px">
              <tr>
                <td style="background:#fff7ed;border:1px solid #fed7aa;border-radius:8px;padding:14px 16px">
                  <p style="margin:0;font-size:13px;color:#92400e;font-family:Arial,sans-serif;line-height:1.5">
                    <strong>&#9888;&nbsp;Luu y:</strong>
                    Lien ket co hieu luc trong <strong>24 gio</strong>.
                    Neu ban khong thuc hien dang ky nay, hay bo qua email nay &mdash; tai khoan se khong duoc kich hoat.
                  </p>
                </td>
              </tr>
            </table>
          </td>
        </tr>

        <!-- Footer -->
        <tr>
          <td style="background:#f8fafc;border-top:1px solid #e2e8f0;
                     padding:20px 48px;text-align:center">
            <p style="margin:0;font-size:12px;color:#94a3b8;font-family:Arial,sans-serif;line-height:1.6">
              &copy; )html"
        << year <<
R"html( <strong style="color:#64748b">Hub32</strong> &mdash; He thong quan ly phong may<br/>
              Email nay duoc gui tu dong, vui long khong tra loi.
            </p>
          </td>
        </tr>

      </table>
      <!-- /Card -->

    </td>
  </tr>
</table>
</body>
</html>
)html";
        msg.bodyHtml = ss.str();
    }

    return Result<EmailMessage>::ok(std::move(msg));
}

} // namespace hub32api::service
