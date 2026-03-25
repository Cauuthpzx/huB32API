/**
 * @file I18n.cpp
 * @brief Implementation of the i18n framework — JSON catalog loading,
 *        Accept-Language negotiation, and placeholder substitution.
 */

#include "PrecompiledHeader.hpp"
#include "internal/I18n.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <regex>

namespace hub32api::core::internal {

// ---------------------------------------------------------------------------
// Static singleton
// ---------------------------------------------------------------------------
std::unique_ptr<I18n> I18n::s_instance;
std::once_flag I18n::s_initFlag;

I18n* I18n::instance()
{
    return s_instance.get();
}

void I18n::init(const std::string& catalogDir, const std::string& defaultLocale)
{
    std::call_once(s_initFlag, [&] {
        s_instance = std::make_unique<I18n>(catalogDir, defaultLocale);
    });
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

I18n::I18n(const std::string& catalogDir, const std::string& defaultLocale)
    : m_defaultLocale(defaultLocale)
{
    namespace fs = std::filesystem;

    if (catalogDir.empty()) {
        spdlog::info("[I18n] no catalog directory configured, i18n disabled");
        return;
    }

    std::error_code ec;
    if (!fs::exists(catalogDir, ec)) {
        spdlog::warn("[I18n] catalog directory '{}' not found", catalogDir);
        return;
    }

    // Scan for *.json files — filename (without extension) is the locale code
    for (const auto& entry : fs::directory_iterator(catalogDir, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".json") continue;

        const std::string locale = entry.path().stem().string();
        loadCatalog(locale, entry.path().string());
    }

    // Sort locales for negotiation
    std::sort(m_locales.begin(), m_locales.end());

    spdlog::info("[I18n] loaded {} locale(s): [{}], default='{}'",
                 m_locales.size(),
                 [&] {
                     std::string joined;
                     for (size_t i = 0; i < m_locales.size(); ++i) {
                         if (i > 0) joined += ", ";
                         joined += m_locales[i];
                     }
                     return joined;
                 }(),
                 m_defaultLocale);
}

// ---------------------------------------------------------------------------
// Catalog loading — flattens nested JSON into dotted keys
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Recursively flattens a JSON object into dotted-key string map.
 *
 * Example: {"error": {"unauthorized": "..."}} → "error.unauthorized" = "..."
 */
void flattenJson(const nlohmann::json& j, const std::string& prefix,
                 std::unordered_map<std::string, std::string>& out)
{
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string fullKey = prefix.empty() ? it.key() : prefix + "." + it.key();
        if (it->is_object()) {
            flattenJson(*it, fullKey, out);
        } else if (it->is_string()) {
            out[fullKey] = it->get<std::string>();
        }
    }
}

} // anonymous namespace

void I18n::loadCatalog(const std::string& locale, const std::string& filePath)
{
    std::ifstream f(filePath);
    if (!f.is_open()) {
        spdlog::warn("[I18n] failed to open catalog '{}'", filePath);
        return;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(f);
        std::unordered_map<std::string, std::string> messages;
        flattenJson(j, "", messages);

        m_catalogs[locale] = std::move(messages);
        m_locales.push_back(locale);

        spdlog::debug("[I18n] loaded locale '{}' with {} keys from '{}'",
                      locale, m_catalogs[locale].size(), filePath);
    }
    catch (const std::exception& ex) {
        spdlog::error("[I18n] failed to parse catalog '{}': {}", filePath, ex.what());
    }
}

// ---------------------------------------------------------------------------
// Message resolution
// ---------------------------------------------------------------------------

std::string I18n::get(const std::string& locale, const std::string& key) const
{
    // 1. Try exact locale
    auto catIt = m_catalogs.find(locale);
    if (catIt != m_catalogs.end()) {
        auto msgIt = catIt->second.find(key);
        if (msgIt != catIt->second.end()) return msgIt->second;
    }

    // 2. Try base locale (e.g., "zh_CN" → "zh")
    auto underscore = locale.find('_');
    if (underscore != std::string::npos) {
        std::string base = locale.substr(0, underscore);
        catIt = m_catalogs.find(base);
        if (catIt != m_catalogs.end()) {
            auto msgIt = catIt->second.find(key);
            if (msgIt != catIt->second.end()) return msgIt->second;
        }
    }

    // 3. Try default locale
    if (locale != m_defaultLocale) {
        catIt = m_catalogs.find(m_defaultLocale);
        if (catIt != m_catalogs.end()) {
            auto msgIt = catIt->second.find(key);
            if (msgIt != catIt->second.end()) return msgIt->second;
        }
    }

    // 4. Return key as-is (fallback)
    return key;
}

std::string I18n::get(const std::string& locale, const std::string& key,
                      const std::vector<std::string>& args) const
{
    return substitute(get(locale, key), args);
}

// ---------------------------------------------------------------------------
// Placeholder substitution
// ---------------------------------------------------------------------------

std::string I18n::substitute(const std::string& tpl, const std::vector<std::string>& args)
{
    std::string result = tpl;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string placeholder = "{" + std::to_string(i) + "}";
        std::string::size_type pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.size(), args[i]);
            pos += args[i].size();
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Accept-Language negotiation (RFC 7231)
// ---------------------------------------------------------------------------

std::string I18n::negotiate(const std::string& acceptLanguage) const
{
    if (acceptLanguage.empty() || m_locales.empty()) return m_defaultLocale;

    // Parse "vi, en-US;q=0.8, zh-CN;q=0.5" into (locale, quality) pairs
    struct LangQ {
        std::string lang;
        double q = 1.0;
    };
    std::vector<LangQ> candidates;

    // Split by comma
    std::string remaining = acceptLanguage;
    while (!remaining.empty()) {
        auto comma = remaining.find(',');
        std::string part = (comma != std::string::npos)
            ? remaining.substr(0, comma)
            : remaining;
        remaining = (comma != std::string::npos)
            ? remaining.substr(comma + 1)
            : "";

        // Trim whitespace
        auto start = part.find_first_not_of(" \t");
        auto end = part.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        part = part.substr(start, end - start + 1);

        // Extract quality value
        LangQ lq;
        auto semi = part.find(';');
        if (semi != std::string::npos) {
            lq.lang = part.substr(0, semi);
            // Trim lang
            auto lend = lq.lang.find_last_not_of(" \t");
            if (lend != std::string::npos) lq.lang = lq.lang.substr(0, lend + 1);

            std::string qpart = part.substr(semi + 1);
            auto qpos = qpart.find("q=");
            if (qpos != std::string::npos) {
                try { lq.q = std::stod(qpart.substr(qpos + 2)); } catch (...) {}
            }
        } else {
            lq.lang = part;
        }

        // Normalize: en-US → en_US, zh-CN → zh_CN
        std::replace(lq.lang.begin(), lq.lang.end(), '-', '_');
        // Lowercase the base part
        auto upos = lq.lang.find('_');
        for (size_t i = 0; i < (upos != std::string::npos ? upos : lq.lang.size()); ++i)
            lq.lang[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(lq.lang[i])));

        candidates.push_back(std::move(lq));
    }

    // Sort by quality descending
    std::sort(candidates.begin(), candidates.end(),
              [](const LangQ& a, const LangQ& b) { return a.q > b.q; });

    // Match against available locales
    for (const auto& c : candidates) {
        // Exact match
        for (const auto& loc : m_locales) {
            if (loc == c.lang) return loc;
        }
        // Base match (vi_VN → vi)
        auto upos = c.lang.find('_');
        if (upos != std::string::npos) {
            std::string base = c.lang.substr(0, upos);
            for (const auto& loc : m_locales) {
                if (loc == base) return loc;
            }
        }
        // Reverse: client sends "zh", we have "zh_CN"
        for (const auto& loc : m_locales) {
            if (loc.find(c.lang) == 0 && (loc.size() == c.lang.size() || loc[c.lang.size()] == '_'))
                return loc;
        }
    }

    return m_defaultLocale;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

std::vector<std::string> I18n::availableLocales() const
{
    return m_locales;
}

} // namespace hub32api::core::internal
