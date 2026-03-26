#include "validation_utils.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <cctype>

namespace hub32api::utils {

bool validate_username(std::string_view username)
{
    if (username.empty() || username.size() > 64) return false;
    return std::all_of(username.begin(), username.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.';
    });
}

bool validate_id(std::string_view id)
{
    if (id.size() != 36) return false;
    for (size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') return false;
        } else {
            const char c = id[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return false;
        }
    }
    return true;
}

std::string sanitize_input(std::string_view input)
{
    std::string result;
    result.reserve(input.size());
    bool inTag = false;
    for (char c : input) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; continue; }
        if (!inTag) result.push_back(c);
    }
    return trim(result);
}

bool check_length(std::string_view s, size_t min_len, size_t max_len)
{
    return s.size() >= min_len && s.size() <= max_len;
}

} // namespace hub32api::utils
