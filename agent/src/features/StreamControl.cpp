#if defined(HUB32_WITH_FFMPEG) && defined(HUB32_WITH_WEBRTC)

#include "hub32agent/features/StreamControl.hpp"
#include "hub32agent/pipeline/StreamPipeline.hpp"
#include "hub32agent/webrtc/WebRtcProducer.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace hub32agent::features {

StreamControl::StreamControl(pipeline::StreamPipeline& pipeline,
                             webrtc::WebRtcProducer& producer)
    : pipeline_(pipeline)
    , producer_(producer)
{
}

std::string StreamControl::execute(const std::string& operation,
                                    const std::map<std::string, std::string>& args)
{
    if (operation == "start") {
        if (pipeline_.isRunning()) {
            return R"({"status":"already_running"})";
        }

        // Connect WebRTC if not connected
        if (!producer_.isConnected()) {
            if (!producer_.connect()) {
                throw std::runtime_error("WebRTC connection failed");
            }
        }

        // Parse optional resolution/fps from args
        pipeline::PipelineConfig cfg;
        auto it = args.find("width");
        if (it != args.end()) cfg.width = std::stoi(it->second);
        it = args.find("height");
        if (it != args.end()) cfg.height = std::stoi(it->second);
        it = args.find("fps");
        if (it != args.end()) cfg.fps = std::stoi(it->second);

        if (!pipeline_.start(cfg)) {
            throw std::runtime_error("StreamPipeline start failed");
        }

        spdlog::info("[StreamControl] started streaming ({}x{} @{} fps, path={})",
                     cfg.width, cfg.height, cfg.fps,
                     pipeline::to_string(pipeline_.activePath()));

        nlohmann::json result;
        result["status"] = "started";
        result["path"]   = pipeline::to_string(pipeline_.activePath());
        result["width"]  = cfg.width;
        result["height"] = cfg.height;
        result["fps"]    = cfg.fps;
        return result.dump();
    }

    if (operation == "stop") {
        if (!pipeline_.isRunning()) {
            return R"({"status":"not_running"})";
        }

        pipeline_.stop();
        spdlog::info("[StreamControl] stopped streaming");
        return R"({"status":"stopped"})";
    }

    if (operation == "status") {
        nlohmann::json result;
        result["running"]   = pipeline_.isRunning();
        result["path"]      = pipeline::to_string(pipeline_.activePath());
        result["connected"] = producer_.isConnected();
        return result.dump();
    }

    throw std::runtime_error("unknown stream-control operation: " + operation);
}

} // namespace hub32agent::features

#endif // HUB32_WITH_FFMPEG && HUB32_WITH_WEBRTC
