#include "string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace hub32api::utils {

std::string trim(std::string_view s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return std::string(s.substr(start, end - start));
}

std::string to_lower(std::string_view s)
{
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::vector<std::string> split(std::string_view s, char delimiter)
{
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == delimiter) {
            parts.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator)
{
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result.append(separator);
        result.append(parts[i]);
    }
    return result;
}

bool starts_with(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string bytes_to_hex(const unsigned char* data, size_t len)
{
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        result.push_back(hex_chars[(data[i] >> 4) & 0x0F]);
        result.push_back(hex_chars[data[i] & 0x0F]);
    }
    return result;
}

std::vector<unsigned char> hex_to_bytes(std::string_view hex)
{
    if (hex.size() % 2 != 0) return {};
    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned char hi = 0, lo = 0;
        char c1 = hex[i], c2 = hex[i + 1];
        if      (c1 >= '0' && c1 <= '9') hi = static_cast<unsigned char>(c1 - '0');
        else if (c1 >= 'a' && c1 <= 'f') hi = static_cast<unsigned char>(c1 - 'a' + 10);
        else if (c1 >= 'A' && c1 <= 'F') hi = static_cast<unsigned char>(c1 - 'A' + 10);
        else return {};
        if      (c2 >= '0' && c2 <= '9') lo = static_cast<unsigned char>(c2 - '0');
        else if (c2 >= 'a' && c2 <= 'f') lo = static_cast<unsigned char>(c2 - 'a' + 10);
        else if (c2 >= 'A' && c2 <= 'F') lo = static_cast<unsigned char>(c2 - 'A' + 10);
        else return {};
        bytes.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return bytes;
}

std::optional<int> safe_stoi(std::string_view s)
{
    if (s.empty() || s.size() > 20) return std::nullopt;
    try {
        size_t pos = 0;
        int val = std::stoi(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        return val;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> safe_stod(std::string_view s)
{
    if (s.empty() || s.size() > 40) return std::nullopt;
    std::string str(s);
    auto lower = to_lower(str);
    if (lower.find("nan") != std::string::npos ||
        lower.find("inf") != std::string::npos) return std::nullopt;
    try {
        size_t pos = 0;
        double val = std::stod(str, &pos);
        if (pos != str.size()) return std::nullopt;
        return val;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace hub32api::utils
