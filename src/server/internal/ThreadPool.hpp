#pragma once

#include <functional>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace veyon32api::server::internal {

// -----------------------------------------------------------------------
// ThreadPool — fixed-size thread pool for HTTP request handling.
// cpp-httplib uses its own threading; this pool is for async background tasks
// (e.g. periodic connection eviction, metrics collection).
// -----------------------------------------------------------------------
class ThreadPool
{
public:
    explicit ThreadPool(int threadCount);
    ~ThreadPool();

    void enqueue(std::function<void()> task);
    void shutdown();
    int  activeCount() const noexcept;

private:
    void workerLoop();

    std::vector<std::thread>          m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex                        m_mutex;
    std::condition_variable           m_cv;
    std::atomic<bool>                 m_stopping{false};
    std::atomic<int>                  m_active{0};
};

} // namespace veyon32api::server::internal
