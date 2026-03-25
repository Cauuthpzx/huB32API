#include "../core/PrecompiledHeader.hpp"
#include "WinServiceAdapter.hpp"
#include "../server/HttpServer.hpp"
#include "hub32api/config/ServerConfig.hpp"

#include <csignal>
#include <iostream>

namespace {

hub32api::HttpServer* g_server = nullptr;

void signalHandler(int sig)
{
    spdlog::info("[main] signal {} received, stopping server", sig);
    if (g_server) g_server->stop();
}

int runServer(const hub32api::ServerConfig& cfg)
{
    hub32api::HttpServer server(cfg);
    g_server = &server;
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);
    bool ok = server.start();
    g_server = nullptr;
    return ok ? 0 : 1;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // Configure spdlog
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    // Parse --config <path> or --install / --uninstall flags
    std::string configPath;
    bool asService   = true;
    bool doInstall   = false;
    bool doUninstall = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--console" || arg == "-c") asService = false;
        else if (arg == "--install")             doInstall = true;
        else if (arg == "--uninstall")           doUninstall = true;
        else if (arg == "--config" && i+1 < argc)
            configPath = argv[++i];
    }

    if (doInstall)   return hub32api::WinServiceAdapter::install(configPath)   ? 0 : 1;
    if (doUninstall) return hub32api::WinServiceAdapter::uninstall()            ? 0 : 1;

    // Load configuration
    auto cfg = configPath.empty()
        ? hub32api::ServerConfig::from_registry()
        : hub32api::ServerConfig::from_file(configPath);

    if (asService) {
        return hub32api::WinServiceAdapter::runAsService([&cfg]{ return runServer(cfg); });
    }

    // Console mode (--console)
    spdlog::info("[main] running in console mode");
    return runServer(cfg);
}
