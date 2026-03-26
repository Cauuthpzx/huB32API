#include "../core/PrecompiledHeader.hpp"
#include "internal/ThreadPool.hpp"

namespace hub32api::server::internal {

ThreadPool::ThreadPool(int threadCount)
{
    for (int i = 0; i < threadCount; ++i)
        m_workers.emplace_back([this]{ workerLoop(); });
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

void ThreadPool::enqueue(std::function<void()> task)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_stopping.load()) {
            spdlog::warn("[ThreadPool] task enqueued after shutdown — ignoring");
            return;
        }
        m_tasks.push(std::move(task));
    }
    m_cv.notify_one();
}

void ThreadPool::shutdown()
{
    bool expected = false;
    if (!m_stopping.compare_exchange_strong(expected, true)) {
        return;  // Already shutting down — prevent double-join
    }
    m_cv.notify_all();
    for (auto& t : m_workers)
        if (t.joinable()) t.join();
}

int ThreadPool::activeCount() const noexcept
{
    return m_active.load();
}

void ThreadPool::workerLoop()
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this]{ return m_stopping || !m_tasks.empty(); });
            if (m_stopping && m_tasks.empty()) return;
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        ++m_active;
        try {
            task();
        } catch (const std::exception& ex) {
            spdlog::error("[ThreadPool] task threw exception: {}", ex.what());
        } catch (...) {
            spdlog::error("[ThreadPool] task threw unknown exception");
        }
        --m_active;
    }
}

} // namespace hub32api::server::internal
