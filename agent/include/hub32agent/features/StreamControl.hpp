#pragma once

#if defined(HUB32_WITH_FFMPEG) && defined(HUB32_WITH_WEBRTC)

#include "hub32agent/FeatureHandler.hpp"
#include <memory>

namespace hub32agent::pipeline { class StreamPipeline; }
namespace hub32agent::webrtc  { class WebRtcProducer; }

namespace hub32agent::features {

/// @brief Feature handler for server-initiated stream start/stop.
///
/// Operations:
///   "start" — start streaming pipeline (args: width, height, fps)
///   "stop"  — stop streaming pipeline
///   "status" — return pipeline state
///
/// Registered with CommandDispatcher as featureUid "stream-control".
class StreamControl : public FeatureHandler
{
public:
    /// @param pipeline Reference to the StreamPipeline (owned by main)
    /// @param producer Reference to the WebRtcProducer (owned by main)
    StreamControl(pipeline::StreamPipeline& pipeline,
                  webrtc::WebRtcProducer& producer);

    std::string featureUid() const override { return "stream-control"; }
    std::string name() const override { return "Stream Control"; }
    std::string execute(const std::string& operation,
                        const std::map<std::string, std::string>& args) override;

private:
    pipeline::StreamPipeline& pipeline_;
    webrtc::WebRtcProducer&   producer_;
};

} // namespace hub32agent::features

#endif // HUB32_WITH_FFMPEG && HUB32_WITH_WEBRTC
