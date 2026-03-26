---
name: threading-pattern
description: Kích hoạt khi viết multithreaded code, mutex, atomic, shared data. Đảm bảo thread safety, no data races, proper synchronization.
---

# Threading Pattern [CCG CP.20, CP.22]

## Quy tắc
- `std::shared_mutex` cho read-heavy data (multiple readers, single writer).
- `std::mutex` + `std::lock_guard` cho exclusive access.
- `std::atomic` cho counters, flags, simple types. KHÔNG volatile.
- KHÔNG truy cập shared mutable state mà không lock.
- Nếu class không thread-safe, ghi rõ trong comment: `// NOT thread-safe`.
- Minimize lock scope — lock, do work, unlock. KHÔNG hold lock qua I/O.

## Patterns
```cpp
// ✅ Read-write lock cho cache
class ComputerCache {
public:
    std::optional<ComputerDto> get(const std::string& id) const {
        std::shared_lock lock(m_mutex);  // multiple readers OK
        auto it = m_data.find(id);
        return it != m_data.end() ? std::optional{it->second} : std::nullopt;
    }
    
    void update(const std::string& id, ComputerDto dto) {
        std::unique_lock lock(m_mutex);  // exclusive writer
        m_data[id] = std::move(dto);
    }
    
private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, ComputerDto> m_data;
};

// ✅ Atomic for simple counters
std::atomic<int64_t> m_requestCount{0};
m_requestCount.fetch_add(1, std::memory_order_relaxed);

// ✅ Scoped lock (locks multiple mutexes without deadlock)
std::scoped_lock lock(m_mutexA, m_mutexB);
```

## KHÔNG LÀM
```cpp
// ❌ volatile cho threading
volatile bool m_running = true;  // dùng std::atomic<bool>

// ❌ Manual lock/unlock (exception-unsafe)
m_mutex.lock();
doWork();  // nếu throw → deadlock!
m_mutex.unlock();

// ❌ Hold lock qua I/O
std::lock_guard lock(m_mutex);
auto result = httpClient.get(url);  // BLOCK lock trong khi network I/O!
```
