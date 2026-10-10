#ifndef GAME_TESTHOOKS_HPP
#define GAME_TESTHOOKS_HPP

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace Game {

/// Parsed SK_ROOM_TELEPORT test hook: "K,R" or "K,R,dx,dy".
struct RoomTeleportSpec {
    bool ok = false;
    long frame = -1;
    int room = -1;
    float dx = 0.0F;
    float dy = 0.0F;
    std::string error; ///< reason when !ok
};

/// Largest accepted |dx| / |dy| of the SK_ROOM_TELEPORT pixel offset.
inline constexpr float kRoomTeleportMaxOffsetPx = 4096.0F;

namespace TestHooksDetail {
inline bool ParseWholeLong(const std::string &s, long &out) {
    if (s.empty()) {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') {
        return false;
    }
    out = v;
    return true;
}
inline bool ParseWholeFloat(const std::string &s, float &out) {
    if (s.empty()) {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0' || !std::isfinite(v) ||
        std::fabs(v) > kRoomTeleportMaxOffsetPx) {
        return false;
    }
    out = v;
    return true;
}
} // namespace TestHooksDetail

/// Strict parse of SK_ROOM_TELEPORT: exactly 2 or 4 comma-separated fields; K and R
/// whole-field integers; dx/dy whole-field finite floats with |v| <= 4096. Anything
/// else rejects the WHOLE hook (ok == false, error set).
inline RoomTeleportSpec ParseRoomTeleport(const std::string &text) {
    RoomTeleportSpec r;
    std::vector<std::string> f;
    std::size_t start = 0;
    while (true) {
        const std::size_t c = text.find(',', start);
        if (c == std::string::npos) {
            f.push_back(text.substr(start));
            break;
        }
        f.push_back(text.substr(start, c - start));
        start = c + 1;
    }
    if (f.size() != 2 && f.size() != 4) {
        r.error = "expected 2 or 4 comma-separated fields";
        return r;
    }
    long room = 0;
    if (!TestHooksDetail::ParseWholeLong(f[0], r.frame)) {
        r.error = "bad frame K";
        return r;
    }
    if (!TestHooksDetail::ParseWholeLong(f[1], room) || room < -2147483647L ||
        room > 2147483647L) {
        r.error = "bad room R";
        return r;
    }
    r.room = static_cast<int>(room);
    if (f.size() == 4) {
        if (!TestHooksDetail::ParseWholeFloat(f[2], r.dx) ||
            !TestHooksDetail::ParseWholeFloat(f[3], r.dy)) {
            r.error = "bad offset (need finite number, |v| <= 4096)";
            r.dx = r.dy = 0.0F;
            return r;
        }
    }
    r.ok = true;
    return r;
}

} // namespace Game

#endif /* GAME_TESTHOOKS_HPP */
