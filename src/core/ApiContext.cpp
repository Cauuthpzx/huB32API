#include "PrecompiledHeader.hpp"
#include "internal/ApiContext.hpp"
#include "hub32api/core/Types.hpp"
#include "hub32api/agent/AgentInfo.hpp"
#include "hub32api/agent/AgentCommand.hpp"

namespace hub32api::core::internal {
// intentionally empty
} // namespace hub32api::core::internal

namespace hub32api {

/** @brief Converts ComputerState enum to string. */
std::string to_string(ComputerState state)
{
    switch (state) {
        case ComputerState::Unknown:       return "unknown";
        case ComputerState::Offline:       return "offline";
        case ComputerState::Online:        return "online";
        case ComputerState::Connecting:    return "connecting";
        case ComputerState::Connected:     return "connected";
        case ComputerState::Disconnecting: return "disconnecting";
    }
    return "unknown";
}

/** @brief Parses a string to ComputerState. */
ComputerState computer_state_from_string(const std::string& s)
{
    if (s == "offline")       return ComputerState::Offline;
    if (s == "online")        return ComputerState::Online;
    if (s == "connecting")    return ComputerState::Connecting;
    if (s == "connected")     return ComputerState::Connected;
    if (s == "disconnecting") return ComputerState::Disconnecting;
    return ComputerState::Unknown;
}

/** @brief Converts FeatureOperation enum to string. */
std::string to_string(FeatureOperation op)
{
    switch (op) {
        case FeatureOperation::Start:      return "start";
        case FeatureOperation::Stop:       return "stop";
        case FeatureOperation::Initialize: return "initialize";
    }
    return "unknown";
}

/** @brief Converts ImageFormat enum to string. */
std::string to_string(ImageFormat fmt)
{
    switch (fmt) {
        case ImageFormat::Png:  return "png";
        case ImageFormat::Jpeg: return "jpeg";
    }
    return "png";
}

/** @brief Parses an image format string to enum. */
ImageFormat image_format_from_string(const std::string& s)
{
    if (s == "jpeg" || s == "jpg") return ImageFormat::Jpeg;
    return ImageFormat::Png;
}

/** @brief Converts AgentState enum to string. */
std::string to_string(AgentState s)
{
    switch (s) {
        case AgentState::Offline: return "offline";
        case AgentState::Online:  return "online";
        case AgentState::Busy:    return "busy";
        case AgentState::Error:   return "error";
    }
    return "offline";
}

/** @brief Parses a string to AgentState. */
AgentState agent_state_from_string(const std::string& s)
{
    if (s == "online")  return AgentState::Online;
    if (s == "busy")    return AgentState::Busy;
    if (s == "error")   return AgentState::Error;
    return AgentState::Offline;
}

/** @brief Converts CommandStatus enum to string. */
std::string to_string(CommandStatus s)
{
    switch (s) {
        case CommandStatus::Pending:    return "pending";
        case CommandStatus::Dispatched: return "dispatched";
        case CommandStatus::Running:    return "running";
        case CommandStatus::Success:    return "success";
        case CommandStatus::Failed:     return "failed";
        case CommandStatus::Timeout:    return "timeout";
    }
    return "pending";
}

/** @brief Parses a string to CommandStatus. */
CommandStatus command_status_from_string(const std::string& s)
{
    if (s == "dispatched") return CommandStatus::Dispatched;
    if (s == "running")    return CommandStatus::Running;
    if (s == "success")    return CommandStatus::Success;
    if (s == "failed")     return CommandStatus::Failed;
    if (s == "timeout")    return CommandStatus::Timeout;
    return CommandStatus::Pending;
}

} // namespace hub32api
