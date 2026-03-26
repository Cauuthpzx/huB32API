#ifdef HUB32_WITH_MEDIASOUP

#include "../core/PrecompiledHeader.hpp"
#include "WorkerChannel.hpp"
#include <uv.h>
#include <chrono>
#include <cstring>

namespace hub32api::media {

namespace {
    // Request timeout in seconds
    constexpr int kRequestTimeoutSeconds = 10; // seconds to wait for worker response
} // namespace

WorkerChannel::WorkerChannel() = default;

WorkerChannel::~WorkerChannel()
{
    close();
}

uint32_t WorkerChannel::genRequestId()
{
    uint32_t id = ++m_nextId;
    if (id == 0) {
        id = ++m_nextId; // Skip 0 — reserved as invalid
    }
    return id;
}

void WorkerChannel::close()
{
    m_closed = true;

    // Wake all pending requests so they unblock
    std::lock_guard lock(m_pendingMutex);
    for (auto& [id, pending] : m_pending) {
        std::lock_guard pLock(pending->mutex);
        pending->completed = true;
        pending->cv.notify_all();
    }
}

std::vector<uint8_t> WorkerChannel::request(uint32_t requestId, const std::vector<uint8_t>& data)
{
    if (m_closed) {
        return {};
    }

    // Register pending request before enqueuing
    auto pending = std::make_shared<PendingRequest>();
    {
        std::lock_guard lock(m_pendingMutex);
        m_pending[requestId] = pending;
    }

    // Enqueue the message for the worker to read
    {
        auto* copy = new uint8_t[data.size()];
        std::memcpy(copy, data.data(), data.size());

        std::lock_guard lock(m_outMutex);
        m_outQueue.push(OutMessage{copy, static_cast<uint32_t>(data.size())});
    }

    // Wake the worker's event loop so it calls channelRead
    if (m_uvAsyncHandle) {
        uv_async_send(static_cast<uv_async_t*>(m_uvAsyncHandle));
    }

    // Wait for response with timeout
    {
        std::unique_lock lock(pending->mutex);
        pending->cv.wait_for(lock, std::chrono::seconds(kRequestTimeoutSeconds),
                             [&] { return pending->completed; });
    }

    // Cleanup pending entry
    {
        std::lock_guard lock(m_pendingMutex);
        m_pending.erase(requestId);
    }

    return std::move(pending->response);
}

void WorkerChannel::notify(const std::vector<uint8_t>& data)
{
    if (m_closed) {
        return;
    }

    auto* copy = new uint8_t[data.size()];
    std::memcpy(copy, data.data(), data.size());

    {
        std::lock_guard lock(m_outMutex);
        m_outQueue.push(OutMessage{copy, static_cast<uint32_t>(data.size())});
    }

    if (m_uvAsyncHandle) {
        uv_async_send(static_cast<uv_async_t*>(m_uvAsyncHandle));
    }
}

// --- Static callbacks for mediasoup_worker_run() ---

void* WorkerChannel::channelRead(uint8_t** message, uint32_t* messageLen,
                                  size_t* messageCtx, const void* handle, void* ctx)
{
    auto* channel = static_cast<WorkerChannel*>(ctx);
    if (!channel) {
        return nullptr;
    }

    // Store the uv_async_t handle so we can wake the loop later
    channel->m_uvAsyncHandle = const_cast<void*>(handle);

    std::lock_guard lock(channel->m_outMutex);
    if (channel->m_outQueue.empty()) {
        return nullptr;
    }

    auto msg = channel->m_outQueue.front();
    channel->m_outQueue.pop();

    *message = msg.data;
    *messageLen = msg.len;
    *messageCtx = 0;

    return reinterpret_cast<void*>(&WorkerChannel::channelReadFree);
}

void WorkerChannel::channelReadFree(uint8_t* message, uint32_t /*messageLen*/, size_t /*messageCtx*/)
{
    delete[] message;
}

void WorkerChannel::channelWrite(const uint8_t* message, uint32_t messageLen, void* ctx)
{
    auto* channel = static_cast<WorkerChannel*>(ctx);
    if (!channel || !message || messageLen == 0) {
        return;
    }

    // Worker uses FinishSizePrefixed — skip first 4 bytes (size prefix)
    constexpr uint32_t kSizePrefixBytes = 4; // bytes for FlatBuffers size prefix
    if (messageLen <= kSizePrefixBytes) {
        return;
    }
    channel->onWorkerMessage(message + kSizePrefixBytes, messageLen - kSizePrefixBytes);
}

void WorkerChannel::onWorkerMessage(const uint8_t* data, uint32_t len)
{
    std::vector<uint8_t> msg(data, data + len);

    // Dispatch to notification callback for all messages.
    // Full FlatBuffers parsing (request/response ID matching) will be
    // added in Phase 3.3 when FBS headers are integrated.
    if (m_notifyCb) {
        m_notifyCb(msg);
    }

    // TODO Phase 3.3: Parse FBS::Message to extract message type:
    //   - Response: match id to m_pending, set response + notify CV
    //   - Notification: forward to notification callback
    //   - Log: forward to spdlog
}

} // namespace hub32api::media

#endif // HUB32_WITH_MEDIASOUP
