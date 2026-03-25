/**
 * @file I18n.hpp
 * @brief Lightweight i18n framework with per-request language negotiation.
 *
 * Features:
 * - JSON message catalogs (one file per locale)
 * - Accept-Language header negotiation (RFC 7231)
 * - Placeholder substitution ({0}, {1}, ...)
 * - Fallback chain: requested locale → default locale → raw key
 * - Thread-safe: catalogs loaded once at startup, read-only at runtime
 */
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace hub32api::core::internal {

/**
 * @brief Internationalization manager for hub32api.
 *
 * Loads JSON message catalogs from a directory and resolves message keys
 * to localized strings. Supports per-request language negotiation via
 * the standard HTTP Accept-Language header.
 *
 * Thread-safe: all public methods are safe to call from any thread after
 * construction. Catalogs are immutable once loaded.
 */
class I18n
{
public:
    /**
     * @brief Constructs the i18n manager and loads all catalogs.
     * @param catalogDir  Directory containing locale JSON files (e.g., "locales/")
     * @param defaultLocale  Fallback locale when negotiation fails (default: "en")
     */
    explicit I18n(const std::string& catalogDir, const std::string& defaultLocale = "en");

    /**
     * @brief Resolves a message key for the given locale.
     * @param locale  Target locale (e.g., "vi", "zh_CN", "en")
     * @param key     Message key (e.g., "error.unauthorized")
     * @return The localized string, or the key itself if not found.
     */
    std::string get(const std::string& locale, const std::string& key) const;

    /**
     * @brief Resolves a message key with placeholder substitution.
     * @param locale  Target locale
     * @param key     Message key
     * @param args    Ordered substitution values for {0}, {1}, ...
     * @return The formatted localized string.
     */
    std::string get(const std::string& locale, const std::string& key,
                    const std::vector<std::string>& args) const;

    /**
     * @brief Parses an Accept-Language header and returns the best matching locale.
     *
     * Implements RFC 7231 quality-value negotiation. Example header:
     *   "vi, en-US;q=0.8, zh-CN;q=0.5"
     *
     * @param acceptLanguage  The raw Accept-Language header value.
     * @return The best matching locale code, or the default locale.
     */
    std::string negotiate(const std::string& acceptLanguage) const;

    /**
     * @brief Returns the list of available locale codes.
     */
    std::vector<std::string> availableLocales() const;

    /**
     * @brief Returns the default locale.
     */
    const std::string& defaultLocale() const { return m_defaultLocale; }

    /**
     * @brief Global singleton accessor.
     *
     * Must be initialized once via init() before use. Returns nullptr
     * if not initialized.
     */
    static I18n* instance();

    /**
     * @brief Initializes the global singleton.
     * @param catalogDir    Directory containing locale JSON files
     * @param defaultLocale Fallback locale
     */
    static void init(const std::string& catalogDir, const std::string& defaultLocale = "en");

private:
    void loadCatalog(const std::string& locale, const std::string& filePath);

    /// @brief Substitutes {0}, {1}, ... placeholders in a template string.
    static std::string substitute(const std::string& tpl, const std::vector<std::string>& args);

    std::string m_defaultLocale;

    /// @brief locale → (key → message)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_catalogs;

    /// @brief Sorted list of available locales for negotiation
    std::vector<std::string> m_locales;

    /// @brief Global singleton
    static std::unique_ptr<I18n> s_instance;
    static std::once_flag s_initFlag;
};

// ---------------------------------------------------------------------------
// Convenience free function for controllers — short and readable
// ---------------------------------------------------------------------------

/**
 * @brief Shorthand: get localized message from the global I18n instance.
 * @param locale  Target locale
 * @param key     Message key
 * @return Localized string or key if not found.
 */
inline std::string tr(const std::string& locale, const std::string& key)
{
    auto* i = I18n::instance();
    return i ? i->get(locale, key) : key;
}

/**
 * @brief Shorthand with placeholder substitution.
 */
inline std::string tr(const std::string& locale, const std::string& key,
                      const std::vector<std::string>& args)
{
    auto* i = I18n::instance();
    return i ? i->get(locale, key, args) : key;
}

} // namespace hub32api::core::internal
