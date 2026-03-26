---
name: memory-safety
description: Kích hoạt khi viết C++ code dùng pointers, dynamic allocation, hoặc resource management. Đảm bảo RAII, smart pointers, no raw new/delete.
---

# Memory Safety [CCG R.11, R.20, R.21]

## Quy tắc tuyệt đối
- `std::unique_ptr` cho single ownership. `std::make_unique` để tạo.
- `std::shared_ptr` CHỈ khi thật sự shared ownership. `std::make_shared` để tạo.
- KHÔNG raw `new` / `delete`. Không ngoại lệ.
- Non-owning observation: raw pointer hoặc reference. KHÔNG smart pointer cho observation.
- RAII cho MỌI resource: file, socket, mutex, handle, database connection.
- Destructor PHẢI giải phóng mọi resource.
- `std::lock_guard` / `std::scoped_lock` cho mutex. KHÔNG manual lock()/unlock().

## Patterns
```cpp
// ✅ unique_ptr
auto encoder = std::make_unique<H264Encoder>();
auto config = std::make_unique<ServerConfig>(path);

// ✅ RAII file handle
class FileHandle {
public:
    explicit FileHandle(const std::string& path) : m_file(std::fopen(path.c_str(), "r")) {
        if (!m_file) throw std::runtime_error("Cannot open file");
    }
    ~FileHandle() { if (m_file) std::fclose(m_file); }
    FILE* get() const { return m_file; }
    HUB32_NO_COPY(FileHandle)
private:
    FILE* m_file;
};

// ✅ Lock guard
void ThreadSafeCache::insert(const std::string& key, Value val) {
    std::lock_guard lock(m_mutex);
    m_data[key] = std::move(val);
}
```

## KHÔNG LÀM
```cpp
// ❌ Raw new
auto* p = new MyClass();
delete p;

// ❌ Manual lock
m_mutex.lock();
doWork();  // nếu throw → deadlock
m_mutex.unlock();

// ❌ shared_ptr khi unique_ptr đủ
auto p = std::make_shared<Config>();  // chỉ 1 owner → dùng unique_ptr
```
