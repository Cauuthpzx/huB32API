#pragma once

#ifdef HUB32_WITH_MEDIASOUP

#include <cstdint>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <unordered_map>
#include <memory>

namespace hub32api::media {

/// @brief Bidirectional message channel for mediasoup worker communication.
/// Implements the ChannelReadFn/ChannelWriteFn callback pattern required
/// by mediasoup_worker_run(). Thread-safe for concurrent request/response.
class WorkerChannel
{
public:
    WorkerChannel();
    ~WorkerChannel();

    // Non-copyable, non-movable (mutex members)
    WorkerChannel(const WorkerChannel&) = delete;
    WorkerChannel& operator=(const WorkerChannel&) = delete;
    WorkerChannel(WorkerChannel&&) = delete;
    WorkerChannel& operator=(WorkerChannel&&) = delete;

    /// @brief Sends a request to the worker and waits for the response.
    /// @param requestId  Unique request ID (must match response.id).
    /// @param data       FlatBuffers-encoded request message.
    /// @return FlatBuffers-encoded response message.
    std::vector<uint8_t> request(uint32_t requestId, const std::vector<uint8_t>& data);

    /// @brief Sends a one-way notification to the worker (no response expected).
    void notify(const std::vector<uint8_t>& data);

    /// @brief Generates a unique request ID (never returns 0).
    uint32_t genRequestId();

    /// @brief Closes the channel. Subsequent reads return nullptr.
    void close();

    /// @brief Returns true if the channel has been closed.
    bool closed() const { return m_closed.load(); }

    // --- Static callbacks passed to mediasoup_worker_run() ---

    /// @brief Worker calls this to read a message from the controller.
    static void* channelRead(uint8_t** message, uint32_t* messageLen,
                             size_t* messageCtx, const void* handle, void* ctx);

    /// @brief Worker calls this to free a message after reading.
    static void channelReadFree(uint8_t* message, uint32_t messageLen, size_t messageCtx);

    /// @brief Worker calls this to write a message to the controller.
    static void channelWrite(const uint8_t* message, uint32_t messageLen, void* ctx);

    /// @brief Callback for async notifications from worker.
    using NotificationCallback = std::function<void(const std::vector<uint8_t>&)>;
    void setNotificationCallback(NotificationCallback cb) { m_notifyCb = std::move(cb); }

private:
    /// @brief Called by channelWrite -- dispatches worker messages.
    void onWorkerMessage(const uint8_t* data, uint32_t len);

    std::atomic<uint32_t> m_nextId{0};
    std::atomic<bool> m_closed{false};

    // Outgoing message queue (controller -> worker)
    struct OutMessage {
        uint8_t* data;    // heap-allocated, freed by channelReadFree
        uint32_t len;     // message length in bytes
    };
    std::mutex m_outMutex;
    std::condition_variable m_outCv;
    std::queue<OutMessage> m_outQueue;

    // Pending request tracking (requestId -> shared state)
    struct PendingRequest {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<uint8_t> response;
        bool completed = false;
    };
    std::mutex m_pendingMutex;
    std::unordered_map<uint32_t, std::shared_ptr<PendingRequest>> m_pending;

    NotificationCallback m_notifyCb;

    // uv_async_t handle for waking worker event loop (set by channelRead)
    void* m_uvAsyncHandle = nullptr;
};

} // namespace hub32api::media

#endif // HUB32_WITH_MEDIASOUP
