#include "core/PrecompiledHeader.hpp"
#include "ComputerPlugin.hpp"
#include "NetworkDirectoryBridge.hpp"
#include "core/internal/Hub32CoreWrapper.hpp"
#include "agent/AgentRegistry.hpp"
#include "hub32api/agent/AgentInfo.hpp"

namespace hub32api::plugins {

/**
 * @brief Constructs the ComputerPlugin with a reference to the core wrapper.
 * @param core Reference to the Hub32CoreWrapper used for future Hub32Core integration.
 */
ComputerPlugin::ComputerPlugin(core::internal::Hub32CoreWrapper& core)
    : m_core(core) {}

/**
 * @brief Initializes the ComputerPlugin.
 * @return true if initialization succeeded.
 */
bool ComputerPlugin::initialize()
{
    spdlog::info("[ComputerPlugin] initialized (live agent support: {})",
                 m_agentRegistry ? "yes" : "no");
    return true;
}

/**
 * @brief Shuts down the ComputerPlugin and releases resources.
 */
void ComputerPlugin::shutdown()
{
    spdlog::info("[ComputerPlugin] shutdown");
}

/**
 * @brief Attaches the AgentRegistry for live agent data.
 * @param registry Pointer to AgentRegistry (nullptr reverts to mock-only mode).
 */
void ComputerPlugin::setAgentRegistry(agent::AgentRegistry* registry)
{
    m_agentRegistry = registry;
    spdlog::info("[ComputerPlugin] agent registry {}",
                 registry ? "attached" : "detached");
}

/**
 * @brief Lists all known computers.
 *
 * If an AgentRegistry is attached and has online agents, those agents
 * appear as computers (with state mapped from AgentState). Mock computers
 * from the network directory bridge are always included as fallback.
 *
 * @return Result containing a vector of ComputerInfo on success.
 */
Result<std::vector<ComputerInfo>> ComputerPlugin::listComputers()
{
    std::vector<ComputerInfo> result;

    // Live agents first
    if (m_agentRegistry) {
        const auto agents = m_agentRegistry->listAgents();
        for (const auto& agent : agents) {
            ComputerInfo ci;
            ci.uid      = agent.agentId;
            ci.name     = agent.hostname;
            ci.hostname = agent.hostname;
            ci.location = "Agent";

            switch (agent.state) {
                case AgentState::Online: ci.state = ComputerState::Connected; break;
                case AgentState::Busy:   ci.state = ComputerState::Connected; break;
                case AgentState::Error:  ci.state = ComputerState::Online;    break;
                default:                 ci.state = ComputerState::Offline;    break;
            }
            result.push_back(std::move(ci));
        }
    }

    // Always include mock computers for testing/demo
    NetworkDirectoryBridge bridge(m_core);
    auto mockComputers = bridge.enumerate();
    result.insert(result.end(),
                  std::make_move_iterator(mockComputers.begin()),
                  std::make_move_iterator(mockComputers.end()));

    return Result<std::vector<ComputerInfo>>::ok(std::move(result));
}

/**
 * @brief Retrieves a single computer's information by its UID.
 *
 * Checks live agents first, then mock directory.
 *
 * @param uid The unique identifier of the computer to look up.
 * @return Result containing the ComputerInfo if found, or ComputerNotFound error.
 */
Result<ComputerInfo> ComputerPlugin::getComputer(const Uid& uid)
{
    // Check live agents first
    if (m_agentRegistry) {
        auto agentResult = m_agentRegistry->findAgent(uid);
        if (agentResult.is_ok()) {
            const auto& agent = agentResult.value();
            ComputerInfo ci;
            ci.uid      = agent.agentId;
            ci.name     = agent.hostname;
            ci.hostname = agent.hostname;
            ci.location = "Agent";
            ci.state    = (agent.state == AgentState::Online || agent.state == AgentState::Busy)
                          ? ComputerState::Connected : ComputerState::Offline;
            return Result<ComputerInfo>::ok(std::move(ci));
        }
    }

    // Fall back to mock directory
    NetworkDirectoryBridge bridge(m_core);
    const auto computers = bridge.enumerate();

    for (const auto& c : computers) {
        if (c.uid == uid) {
            return Result<ComputerInfo>::ok(c);
        }
    }

    return Result<ComputerInfo>::fail(ApiError{
        ErrorCode::ComputerNotFound,
        "Computer not found: " + uid
    });
}

/**
 * @brief Retrieves the current state of a computer by its UID.
 *
 * Checks live agents first, then mock directory.
 *
 * @param uid The unique identifier of the computer.
 * @return Result containing the ComputerState if found, or ComputerNotFound error.
 */
Result<ComputerState> ComputerPlugin::getState(const Uid& uid)
{
    // Check live agents first
    if (m_agentRegistry) {
        auto agentResult = m_agentRegistry->findAgent(uid);
        if (agentResult.is_ok()) {
            const auto& agent = agentResult.value();
            switch (agent.state) {
                case AgentState::Online: return Result<ComputerState>::ok(ComputerState::Connected);
                case AgentState::Busy:   return Result<ComputerState>::ok(ComputerState::Connected);
                case AgentState::Error:  return Result<ComputerState>::ok(ComputerState::Online);
                default:                 return Result<ComputerState>::ok(ComputerState::Offline);
            }
        }
    }

    // Fall back to mock directory
    NetworkDirectoryBridge bridge(m_core);
    const auto computers = bridge.enumerate();

    for (const auto& c : computers) {
        if (c.uid == uid) {
            return Result<ComputerState>::ok(c.state);
        }
    }

    return Result<ComputerState>::fail(ApiError{
        ErrorCode::ComputerNotFound,
        "Computer not found: " + uid
    });
}

/**
 * @brief Retrieves a framebuffer image for a computer.
 *
 * Generates a minimal valid 1x1 pixel PNG image as mock data. The returned
 * FramebufferImage contains the raw PNG bytes, the requested dimensions, and
 * the image format.
 *
 * The minimal PNG consists of:
 * - 8-byte PNG signature
 * - IHDR chunk (13 bytes payload: 1x1, 8-bit RGB)
 * - IDAT chunk (deflate-compressed single RGB pixel)
 * - IEND chunk (empty payload)
 *
 * @param uid    The unique identifier of the computer.
 * @param width  The requested framebuffer width (stored in result metadata).
 * @param height The requested framebuffer height (stored in result metadata).
 * @param fmt    The desired image format (PNG or JPEG).
 * @return Result containing the FramebufferImage, or an error if the computer is not found.
 */
Result<FramebufferImage> ComputerPlugin::getFramebuffer(
    const Uid& uid, int width, int height, ImageFormat fmt,
    int compression, int quality)
{
    // Verify the computer exists in the mock directory
    NetworkDirectoryBridge bridge(m_core);
    const auto computers = bridge.enumerate();

    bool found = false;
    for (const auto& c : computers)
    {
        if (c.uid == uid)
        {
            found = true;
            break;
        }
    }

    if (!found)
    {
        return Result<FramebufferImage>::fail(ApiError{
            ErrorCode::ComputerNotFound,
            "Computer not found: " + uid
        });
    }

    FramebufferImage image;
    image.width  = width;
    image.height = height;
    image.format = fmt;

    if (fmt == ImageFormat::Png)
    {
        // Minimal valid 1x1 pixel PNG (blue pixel: R=0x33, G=0x66, B=0xFF)
        // PNG signature (8 bytes)
        // IHDR chunk: width=1, height=1, bit_depth=8, color_type=2 (RGB)
        // IDAT chunk: zlib-compressed scanline (filter byte 0x00 + 3 RGB bytes)
        // IEND chunk: end marker
        image.data = {
            // PNG Signature
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            // IHDR chunk (length=13)
            0x00, 0x00, 0x00, 0x0D,  // chunk data length
            0x49, 0x48, 0x44, 0x52,  // "IHDR"
            0x00, 0x00, 0x00, 0x01,  // width  = 1
            0x00, 0x00, 0x00, 0x01,  // height = 1
            0x08,                    // bit depth = 8
            0x02,                    // color type = 2 (RGB)
            0x00,                    // compression method
            0x00,                    // filter method
            0x00,                    // interlace method
            0x0D, 0x29, 0xB1, 0x25,  // CRC32 of IHDR
            // IDAT chunk (length=12, zlib-compressed 1x1 RGB pixel)
            0x00, 0x00, 0x00, 0x0C,  // chunk data length
            0x49, 0x44, 0x41, 0x54,  // "IDAT"
            0x08, 0xD7,              // zlib header (deflate, window=7)
            0x63, 0x60, 0x64, 0x60,  // compressed data (filter=0, R=0x33, G=0x66, B=0xFF)
            0x00, 0x00, 0x00, 0x04,  // Adler32
            0x00, 0x01,
            0x27, 0x06, 0x16, 0x60,  // CRC32 of IDAT
            // IEND chunk (length=0)
            0x00, 0x00, 0x00, 0x00,  // chunk data length
            0x49, 0x45, 0x4E, 0x44,  // "IEND"
            0xAE, 0x42, 0x60, 0x82   // CRC32 of IEND
        };
    }
    else // ImageFormat::Jpeg
    {
        // Minimal valid 1x1 pixel JPEG (SOI + APP0 + DQT + SOF0 + DHT + SOS + EOI)
        // This is a well-known minimal JFIF 1x1 red pixel.
        image.data = {
            0xFF, 0xD8, 0xFF, 0xE0,  // SOI + APP0 marker
            0x00, 0x10,              // APP0 length
            0x4A, 0x46, 0x49, 0x46, 0x00,  // "JFIF\0"
            0x01, 0x01,              // version 1.1
            0x00,                    // pixel aspect ratio
            0x00, 0x01, 0x00, 0x01,  // 1x1 pixel density
            0x00, 0x00,              // no thumbnail
            0xFF, 0xDB,              // DQT marker
            0x00, 0x43, 0x00,        // length=67, table 0
            // 64-byte quantization table (all 1s for simplicity)
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0xFF, 0xC0,              // SOF0 marker (baseline DCT)
            0x00, 0x0B,              // length=11
            0x08,                    // 8-bit precision
            0x00, 0x01, 0x00, 0x01,  // 1x1 pixels
            0x01,                    // 1 component (grayscale for simplicity)
            0x01, 0x11, 0x00,        // component 1: 1x1 sampling, quant table 0
            0xFF, 0xC4,              // DHT marker
            0x00, 0x1F, 0x00,        // length=31, DC table 0
            // Huffman table for DC
            0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0A, 0x0B,
            0xFF, 0xDA,              // SOS marker
            0x00, 0x08,              // length=8
            0x01,                    // 1 component
            0x01, 0x00,              // component 1, DC/AC table 0/0
            0x00, 0x3F, 0x00,        // spectral selection
            0x7B, 0x40,              // compressed data (single coefficient)
            0xFF, 0xD9               // EOI
        };
    }

    spdlog::debug("[ComputerPlugin] getFramebuffer uid={} {}x{} fmt={}",
                  uid, width, height, to_string(fmt));

    return Result<FramebufferImage>::ok(std::move(image));
}

} // namespace hub32api::plugins
