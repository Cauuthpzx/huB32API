#pragma once

#include <memory>
#include "veyon32api/config/ServerConfig.hpp"

namespace veyon32api {

// -----------------------------------------------------------------------
// HttpServer — top-level HTTP server orchestrator.
// Owns: httplib::Server, Router, all controllers and middlewares.
// Lifecycle: create → start() (blocks) → stop() (from signal handler).
// -----------------------------------------------------------------------
class HttpServer
{
public:
    explicit HttpServer(const ServerConfig& cfg);
    ~HttpServer();

    // Starts listening — blocks until stop() is called
    bool start();
    void stop();

    bool isRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace veyon32api
