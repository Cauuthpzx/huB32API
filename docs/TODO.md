# Hub32API Technical Debt & TODO

---

## 1. Rename CryptoUtils to snake_case API

**Priority:** LOW (cosmetic, non-blocking)

**Files:** `src/core/internal/CryptoUtils.hpp`, `src/core/CryptoUtils.cpp`

**Changes:**
- `generateUuid()` -> `generate_uuid()`
- `randomBytes()` -> `random_bytes()`

**Callers to update (search `CryptoUtils::generate` and `CryptoUtils::random`):**
- `src/auth/JwtAuth.cpp`
- `src/agent/AgentRegistry.cpp`
- `src/api/v1/controllers/AgentController.cpp`
- `src/plugins/feature/FeaturePlugin.cpp`
- `src/media/MockSfuBackend.cpp`
- `src/db/TeacherRepository.cpp`
- `src/db/SchoolRepository.cpp`
- `src/db/ComputerRepository.cpp`
- `src/db/LocationRepository.cpp`
- `src/core/ConnectionPool.cpp`
- `tests/unit/core/test_crypto_utils.cpp`

**Reason:** CLAUDE.md mandates snake_case for functions. Current camelCase was inherited from early development.

---

## 2. Extract duplicated helpers to shared location

**Priority:** MEDIUM (DRY violation, but working code)

### 2a. `requireAdmin()` duplicated in 2 controllers

**Found in:**
- `src/api/v1/controllers/TeacherController.cpp` (anonymous namespace)
- `src/api/v1/controllers/SchoolController.cpp` (anonymous namespace)

**Fix:** Extract to `src/api/common/AuthHelper.hpp` or integrate into `AuthMiddleware` as a reusable check.

### 2b. `getLocale()` duplicated in 6 files

**Found in:**
- `src/api/v1/controllers/AuthController.cpp`
- `src/api/v1/controllers/TeacherController.cpp`
- `src/api/v1/controllers/SchoolController.cpp`
- `src/api/v1/controllers/ComputerController.cpp`
- `src/api/v1/middleware/AuthMiddleware.cpp`
- `src/api/v1/middleware/RateLimitMiddleware.cpp`

**Fix:** Extract to `src/api/common/LocaleHelper.hpp` as an inline function.

**Reason:** Identical 4-line function copy-pasted 6 times. Any change to locale negotiation logic requires updating all 6 copies.
