#include "core/PrecompiledHeader.hpp"
#include "SessionPlugin.hpp"
#include "core/internal/Hub32CoreWrapper.hpp"

namespace hub32api::plugins {

/**
 * @brief Constructs the SessionPlugin with a reference to the core wrapper.
 * @param core Reference to the Hub32CoreWrapper used for future Hub32Core integration.
 */
SessionPlugin::SessionPlugin(core::internal::Hub32CoreWrapper& core)
    : m_core(core) {}

namespace {

/**
 * @brief Mock hostname lookup table keyed by computer UID.
 *
 * Mirrors the same mock data used by NetworkDirectoryBridge so that
 * session information stays consistent with the computer directory.
 */
const std::unordered_map<std::string, std::string>& mockHostnames()
{
    static const std::unordered_map<std::string, std::string> table = {
        {"pc-001", "192.168.1.101"},
        {"pc-002", "192.168.1.102"},
        {"pc-003", "192.168.1.103"},
        {"pc-004", "192.168.1.201"},
        {"pc-005", "192.168.1.202"},
        {"pc-006", "10.0.0.50"},
    };
    return table;
}

/**
 * @brief Looks up the mock hostname for a given computer UID.
 * @param computerUid The computer's unique identifier.
 * @return The hostname string, or "0.0.0.0" if the UID is not found.
 */
std::string hostnameFor(const std::string& computerUid)
{
    const auto& table = mockHostnames();
    auto it = table.find(computerUid);
    if (it != table.end())
    {
        return it->second;
    }
    return "0.0.0.0";
}

/**
 * @brief Computes a simple hash of a string to generate a mock session ID.
 *
 * Uses std::hash<std::string> and maps the result to a positive integer
 * in a reasonable range for session IDs.
 *
 * @param s The input string (typically a computer UID).
 * @return A deterministic positive integer derived from the input.
 */
int simpleHash(const std::string& s)
{
    auto h = std::hash<std::string>{}(s);
    return static_cast<int>((h % 99999) + 1);
}

} // anonymous namespace

/**
 * @brief Retrieves session information for a computer.
 *
 * Returns mock SessionInfo with a deterministic session ID derived from
 * the computer UID, a simulated student user, and the computer's hostname
 * as the client address.
 *
 * When Hub32Core is linked, this will delegate to
 * ComputerControlInterface::sessionInfo().
 *
 * @param computerUid The unique identifier of the target computer.
 * @return Result containing the SessionInfo on success, or ComputerNotFound error.
 */
Result<SessionInfo> SessionPlugin::getSession(const Uid& computerUid)
{
    const auto& table = mockHostnames();
    if (table.find(computerUid) == table.end())
    {
        return Result<SessionInfo>::fail(ApiError{
            ErrorCode::ComputerNotFound,
            "Computer not found: " + computerUid
        });
    }

    SessionInfo info;
    info.sessionId     = simpleHash(computerUid);
    info.userLogin     = "student01";
    info.userFullName  = "Student User";
    info.clientAddress = hostnameFor(computerUid);
    info.uptimeSeconds = 3600;
    info.sessionType   = "console";

    return Result<SessionInfo>::ok(std::move(info));
}

/**
 * @brief Retrieves user information for the logged-in user on a computer.
 *
 * Returns mock UserInfo representing a student account in the SCHOOL
 * domain with typical group memberships.
 *
 * When Hub32Core is linked, this will delegate to
 * ComputerControlInterface::userLoginName() and userFullName().
 *
 * @param computerUid The unique identifier of the target computer.
 * @return Result containing the UserInfo on success, or ComputerNotFound error.
 */
Result<UserInfo> SessionPlugin::getUser(const Uid& computerUid)
{
    const auto& table = mockHostnames();
    if (table.find(computerUid) == table.end())
    {
        return Result<UserInfo>::fail(ApiError{
            ErrorCode::ComputerNotFound,
            "Computer not found: " + computerUid
        });
    }

    UserInfo user;
    user.login    = "student01";
    user.fullName = "Student User";
    user.domain   = "SCHOOL";
    user.groups   = {"students", "lab-users"};

    return Result<UserInfo>::ok(std::move(user));
}

/**
 * @brief Retrieves the list of screens (monitors) attached to a computer.
 *
 * Returns a single mock 1920x1080 screen at position (0, 0), representing
 * a typical Full-HD display.
 *
 * When Hub32Core is linked, this will delegate to
 * ComputerControlInterface::screens().
 *
 * @param computerUid The unique identifier of the target computer.
 * @return Result containing a vector of ScreenRect on success, or ComputerNotFound error.
 */
Result<std::vector<ScreenRect>> SessionPlugin::getScreens(const Uid& computerUid)
{
    const auto& table = mockHostnames();
    if (table.find(computerUid) == table.end())
    {
        return Result<std::vector<ScreenRect>>::fail(ApiError{
            ErrorCode::ComputerNotFound,
            "Computer not found: " + computerUid
        });
    }

    ScreenRect screen;
    screen.x      = 0;
    screen.y      = 0;
    screen.width  = 1920;
    screen.height = 1080;

    return Result<std::vector<ScreenRect>>::ok({screen});
}

} // namespace hub32api::plugins
