/**
 * @file FrameQueue.hpp
 * @brief Bounded blocking queue for the 3-thread capture/encode/send pipeline.
 *
 * Decouples capture from encoding: prevents DXGI
 * AcquireNextFrame timeouts caused by slow encoding.
 *
 * Thread safety: push() and pop() are fully thread-safe.
 */

#pragma once

#if defined(HUB32_WITH_FFMPEG) && defined(HUB32_WITH_WEBRTC)

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

namespace hub32agent::pipeline {

/// @brief A raw BGRA frame ready for color conversion + encoding.
struct RawFrame {
    std::vector<uint8_t> bgra;   ///< BGRA pixel data (width * height * 4 bytes)
    int                  width;  ///< frame width in pixels
    int                  height; ///< frame height in pixels
    int64_t              ptsUs;  ///< presentation timestamp in microseconds
};

/// @brief Bounded blocking queue used between capture and encode threads.
/// Drops the oldest frame if the queue is full (frame drop on overload).
///
/// Thread safety: push() and pop() are thread-safe.
template<typename T, size_t Capacity>
class BoundedQueue
{
public:
    /// @brief Push item. If queue is full, drops the oldest item (frame drop).
    void push(T item)
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        if (m_q.size() >= Capacity) {
            m_q.pop(); // drop oldest — prevents capture stall
        }
        m_q.push(std::move(item));
        lk.unlock();
        m_cv.notify_one();
    }

    /// @brief Blocking pop. Returns false if stop() was called and queue is empty.
    bool pop(T& out)
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        m_cv.wait(lk, [this] { return !m_q.empty() || m_stopped; });
        if (m_stopped && m_q.empty()) return false;
        out = std::move(m_q.front());
        m_q.pop();
        return true;
    }

    /// @brief Unblocks all waiting pop() calls. Call before joining threads.
    void stop()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_stopped = true;
        m_cv.notify_all();
    }

    /// @brief Resets queue state for reuse. Not thread-safe — call only when idle.
    void reset()
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        while (!m_q.empty()) m_q.pop();
        m_stopped = false;
    }

    /// @brief Returns current queue size. For diagnostics only.
    size_t size() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_q.size();
    }

private:
    std::queue<T>           m_q;
    mutable std::mutex      m_mtx;
    std::condition_variable m_cv;
    bool                    m_stopped = false;
};

/// @brief Frame queue between capture and encode threads (max 4 frames buffered).
using RawFrameQueue = BoundedQueue<RawFrame, 4>;

} // namespace hub32agent::pipeline

#endif // HUB32_WITH_FFMPEG && HUB32_WITH_WEBRTC
