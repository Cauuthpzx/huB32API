# Hub32API i18n Implementation Status

## Architecture

Per-request language negotiation via HTTP `Accept-Language` header.
Framework: `I18n` class with JSON message catalogs, RFC 7231 negotiation.
Fallback chain: exact locale → base locale → default locale → raw key.

## Available Locales

| Locale | Language | Keys | Status |
|--------|----------|------|--------|
| `en` | English | 70+ | Complete (default) |
| `vi` | Vietnamese | 70+ | Complete |
| `zh_CN` | Simplified Chinese | 70+ | Complete |

## Files with i18n (tr() calls wired)

### Controllers (user-facing API responses)
- [x] `src/api/v1/controllers/AuthController.cpp` — login/logout errors
- [x] `src/api/v1/controllers/ComputerController.cpp` — list/get/info errors
- [x] `src/api/v1/controllers/FeatureController.cpp` — list/get/control errors
- [x] `src/api/v1/controllers/FramebufferController.cpp` — capture errors
- [x] `src/api/v1/controllers/SessionController.cpp` — session/user/screens errors
- [x] `src/api/v1/controllers/AgentController.cpp` — register/status/command errors
- [x] `src/api/v2/controllers/BatchController.cpp` — batch validation errors
- [x] `src/api/v2/controllers/LocationController.cpp` — location errors
- [x] `src/api/v2/controllers/MetricsController.cpp` — health status strings

### Middleware
- [x] `src/api/v1/middleware/AuthMiddleware.cpp` — 401 auth errors
- [x] `src/api/v1/middleware/RateLimitMiddleware.cpp` — 429 rate limit errors

### NOT i18n (intentionally)
- [ ] `src/api/v1/middleware/CorsMiddleware.cpp` — no user-facing strings
- [ ] `src/api/v1/middleware/LoggingMiddleware.cpp` — no user-facing strings
- [ ] All spdlog log messages — ops/debugging, stay English
- [ ] `src/server/Router.cpp` — only resolveLocale helper, routes delegate to controllers

## i18n Key Categories

| Prefix | Count | Purpose |
|--------|-------|---------|
| `error.*` | 40 | API error responses (title + detail) |
| `auth.*` | 2 | Auth method descriptions |
| `health.*` | 3 | Health endpoint status strings |
| `config.*` | 9 | Config validation messages |
| `debug.*` | 12 | Debug page labels |
| `service.*` | 6 | Service management messages |

## How to Add a New Locale

1. Copy `locales/en.json` to `locales/{locale_code}.json`
2. Translate all values (keys stay the same)
3. Restart server — `I18n` auto-discovers `.json` files in `localesDir`

Example: Add Korean
```bash
cp locales/en.json locales/ko.json
# Edit ko.json with Korean translations
# Restart hub32api-service
```

## How to Add a New i18n Key

1. Add key to ALL locale files (`locales/en.json`, `locales/vi.json`, `locales/zh_CN.json`)
2. Use in code: `tr(lang, "error.new_key")` or `tr(lang, "error.new_key", {arg0})`

## Config

```json
{
  "i18n": {
    "localesDir": "locales",
    "defaultLocale": "en"
  }
}
```

## Unused Keys (reserved for future)

These keys exist in catalogs but are not yet used in code:
- `error.token_revoked/expired/empty/decode_failed/verification_failed/missing_claim` — JWT detail errors (used when error messages are enhanced)
- `auth.method_hub32_key/method_logon` — auth method descriptions for future UI
- `config.*` — config validation (used when ConfigValidator is wired)
- `debug.*` — debug HTML page (used when debug endpoint is localized)
- `service.*` — Windows service messages (used when service management is localized)
