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
inline bool ParseWholeFloat(const std::string &s, float &out,
                            float maxAbs = kRoomTeleportMaxOffsetPx) {
    if (s.empty()) {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0' || !std::isfinite(v) ||
        std::fabs(v) > maxAbs) {
        return false;
    }
    out = v;
    return true;
}
inline std::vector<std::string> SplitCommas(const std::string &text) {
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
    return f;
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

/// Parsed SK_FORCE_DASH test hook: "K,dx,dy" -- on frame K the player dashes (the SAME
/// Sim::ApplyDash the C02 ultimate uses) along (dx, dy) without needing the hero, the mouse or
/// the skill button. P2.18 evidence only; NO-OP unless set.
struct ForceDashSpec {
    bool ok = false;
    long frame = -1;
    float dx = 0.0F;
    float dy = 0.0F;
    std::string error;
};

/// Strict parse of SK_FORCE_DASH: exactly 3 fields, K a whole integer >= 0, dx/dy whole-field
/// finite floats with |v| <= 4096 and (dx, dy) != (0, 0). Anything else rejects the hook.
inline ForceDashSpec ParseForceDash(const std::string &text) {
    ForceDashSpec r;
    const std::vector<std::string> f = TestHooksDetail::SplitCommas(text);
    if (f.size() != 3) {
        r.error = "expected 3 comma-separated fields K,dx,dy";
        return r;
    }
    if (!TestHooksDetail::ParseWholeLong(f[0], r.frame) || r.frame < 0) {
        r.error = "bad frame K";
        return r;
    }
    if (!TestHooksDetail::ParseWholeFloat(f[1], r.dx) || !TestHooksDetail::ParseWholeFloat(f[2], r.dy) ||
        (r.dx == 0.0F && r.dy == 0.0F)) {
        r.error = "bad direction (need finite, non-zero, |v| <= 4096)";
        r.dx = r.dy = 0.0F;
        return r;
    }
    r.ok = true;
    return r;
}

/// Parsed SK_WALK_DIR test hook: "dx,dy" -- the player holds this direction every frame (as if the
/// movement keys were pressed), for the P2.18 wall / dt-spike evidence. NO-OP unless set.
struct WalkDirSpec {
    bool ok = false;
    float dx = 0.0F;
    float dy = 0.0F;
    std::string error;
};

inline WalkDirSpec ParseWalkDir(const std::string &text) {
    WalkDirSpec r;
    const std::vector<std::string> f = TestHooksDetail::SplitCommas(text);
    if (f.size() != 2) {
        r.error = "expected 2 comma-separated fields dx,dy";
        return r;
    }
    if (!TestHooksDetail::ParseWholeFloat(f[0], r.dx) || !TestHooksDetail::ParseWholeFloat(f[1], r.dy) ||
        (r.dx == 0.0F && r.dy == 0.0F)) {
        r.error = "bad direction (need finite, non-zero, |v| <= 4096)";
        r.dx = r.dy = 0.0F;
        return r;
    }
    r.ok = true;
    return r;
}

/// Parsed SK_FORCE_FIRE test hook: "K,dx,dy" or "K,dx,dy,N" -- from frame K the player aims along
/// (dx, dy) and holds fire for N frames (default: until the run ends), without the mouse. P3.0c
/// evidence (bullet heading at 4+ directions); NO-OP unless set.
struct ForceFireSpec {
    bool ok = false;
    long frame = -1;
    float dx = 0.0F;
    float dy = 0.0F;
    /// Unit aim vector computed with a STABLE normalisation (see NormalizeHookDir): a direction such as
    /// (1e-30, 0) passes the range check, but squaring it in float underflows to a zero length.
    float ux = 0.0F;
    float uy = 0.0F;
    long count = 1000000000L; ///< frames to hold fire (>= 1).
    std::string error;
};

/// SLICE 4 (characters): SK_FORCE_AIM=K,dx,dy[,N] -- from frame K aim along (dx, dy) for N frames (default:
/// until the run ends) WITHOUT firing, so the held-weapon rotation and the body facing can be captured at
/// chosen angles. Same strict grammar and result as SK_FORCE_FIRE (ParseForceFire); NO-OP unless set.
/// Applied to the aim only (the fire hook still wins while it is active).

/// Unit vector of (dx, dy) without squaring tiny / huge components in float: both are first divided
/// by max(|dx|, |dy|) and the length is taken in double. Returns false (outputs 0) when the vector is
/// zero, non-finite, or still has no usable length.
inline bool NormalizeHookDir(double dx, double dy, float &ux, float &uy) {
    ux = uy = 0.0F;
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return false;
    }
    const double m = std::fmax(std::fabs(dx), std::fabs(dy));
    if (!(m > 0.0) || !std::isfinite(m)) {
        return false;
    }
    const double sx = dx / m;
    const double sy = dy / m;
    const double len = std::sqrt(sx * sx + sy * sy); // in [1, sqrt(2)]
    if (!(len > 0.0) || !std::isfinite(len)) {
        return false;
    }
    ux = static_cast<float>(sx / len);
    uy = static_cast<float>(sy / len);
    return true;
}

/// Strict parse of SK_FORCE_FIRE: 3 or 4 fields; K a whole integer >= 0; dx/dy whole-field finite
/// floats with |v| <= 4096, not both zero; N a whole integer >= 1. Anything else rejects the hook.
inline ForceFireSpec ParseForceFire(const std::string &text) {
    ForceFireSpec r;
    const std::vector<std::string> f = TestHooksDetail::SplitCommas(text);
    if (f.size() != 3 && f.size() != 4) {
        r.error = "expected 3 or 4 comma-separated fields K,dx,dy[,N]";
        return r;
    }
    if (!TestHooksDetail::ParseWholeLong(f[0], r.frame) || r.frame < 0) {
        r.error = "bad frame K";
        return r;
    }
    if (!TestHooksDetail::ParseWholeFloat(f[1], r.dx) || !TestHooksDetail::ParseWholeFloat(f[2], r.dy) ||
        (r.dx == 0.0F && r.dy == 0.0F)) {
        r.error = "bad direction (need finite, non-zero, |v| <= 4096)";
        r.dx = r.dy = 0.0F;
        return r;
    }
    if (!NormalizeHookDir(r.dx, r.dy, r.ux, r.uy)) {
        r.error = "direction has no usable length";
        r.dx = r.dy = 0.0F;
        return r;
    }
    if (f.size() == 4) {
        long n = 0;
        if (!TestHooksDetail::ParseWholeLong(f[3], n) || n < 1) {
            r.error = "bad frame count N (need whole number >= 1)";
            r.dx = r.dy = r.ux = r.uy = 0.0F; // a rejected spec carries no direction (slice 4: SK_FORCE_AIM shares this)
            return r;
        }
        r.count = n;
    }
    r.ok = true;
    return r;
}

/// SK_FORCE_AIM shares SK_FORCE_FIRE's grammar (see above).
inline ForceFireSpec ParseForceAim(const std::string &text) {
    return ParseForceFire(text);
}

/// Parsed SK_DT_SPIKE test hook: "K,ms" -- frame K runs with a frame delta of @c ms instead of the
/// wall-clock / SK_DT_MS delta (a simulated hitch). 0 < ms <= 3.6e6.
struct DtSpikeSpec {
    bool ok = false;
    long frame = -1;
    float ms = 0.0F;
    std::string error;
};

inline DtSpikeSpec ParseDtSpike(const std::string &text) {
    DtSpikeSpec r;
    const std::vector<std::string> f = TestHooksDetail::SplitCommas(text);
    if (f.size() != 2) {
        r.error = "expected 2 comma-separated fields K,ms";
        return r;
    }
    if (!TestHooksDetail::ParseWholeLong(f[0], r.frame) || r.frame < 0) {
        r.error = "bad frame K";
        return r;
    }
    if (!TestHooksDetail::ParseWholeFloat(f[1], r.ms, 3.6e6F) || !(r.ms > 0.0F)) {
        r.error = "bad ms (need finite, 0 < ms <= 3.6e6)";
        r.ms = 0.0F;
        return r;
    }
    r.ok = true;
    return r;
}

/// Parsed SK_TRACE_POS test hook: "F1,F2,..." -- on each listed frame the scene logs the player's
/// and every hostile's position with an "overlaps a blocker" flag (P2.18 evidence). At most 32
/// non-negative integer frames; anything else rejects the whole hook.
struct FrameListSpec {
    bool ok = false;
    std::vector<long> frames;
    std::string error;
};

inline FrameListSpec ParseFrameList(const std::string &text) {
    FrameListSpec r;
    const std::vector<std::string> f = TestHooksDetail::SplitCommas(text);
    if (f.empty() || f.size() > 32) {
        r.error = "expected 1..32 comma-separated frames";
        return r;
    }
    for (const std::string &item : f) {
        long v = 0;
        if (!TestHooksDetail::ParseWholeLong(item, v) || v < 0) {
            r.error = "bad frame '" + item + "'";
            r.frames.clear();
            return r;
        }
        r.frames.push_back(v);
    }
    r.ok = true;
    return r;
}

/// Parsed SK_STING_STAND test hook (P2.20): "K,R" -- on frame K the hero is put on the middle trap cell of
/// room R's sting01 group (the room must still be started by the normal StartRoom flow), so the damage
/// ticks can be logged. NO-OP unless set; invalid -> the hook stays unset.
struct StingStandSpec {
    bool ok = false;
    long frame = -1;
    int room = -1;
    std::string error;
};

/// Strict parse of SK_STING_STAND: exactly 2 fields, K a whole integer >= 0, R a whole integer >= 0.
inline StingStandSpec ParseStingStand(const std::string &text) {
    StingStandSpec r;
    const std::vector<std::string> f = TestHooksDetail::SplitCommas(text);
    if (f.size() != 2) {
        r.error = "expected 2 comma-separated fields K,R";
        return r;
    }
    long room = 0;
    if (!TestHooksDetail::ParseWholeLong(f[0], r.frame) || r.frame < 0) {
        r.error = "bad frame K";
        r.frame = -1;
        return r;
    }
    if (!TestHooksDetail::ParseWholeLong(f[1], room) || room < 0 || room > 2147483647L) {
        r.error = "bad room R";
        r.frame = -1;
        return r;
    }
    r.room = static_cast<int>(room);
    r.ok = true;
    return r;
}

/// Parsed SK_CHEST_STAND test hook (P2.20): "K,D" -- on frame K the hero is put D px below the FIRST chest of
/// the floor (D = 0 stands on it, which opens it; D > the chest's open range leaves it closed), so the closed
/// and the opening chest can be photographed. NO-OP unless set; invalid -> the hook stays unset.
struct ChestStandSpec {
    bool ok = false;
    long frame = -1;
    float below = 0.0F;
    std::string error;
};

/// Strict parse of SK_CHEST_STAND: exactly 2 fields, K a whole integer >= 0, D a whole-field finite float in
/// [0, 4096].
inline ChestStandSpec ParseChestStand(const std::string &text) {
    ChestStandSpec r;
    const std::vector<std::string> f = TestHooksDetail::SplitCommas(text);
    if (f.size() != 2) {
        r.error = "expected 2 comma-separated fields K,D";
        return r;
    }
    if (!TestHooksDetail::ParseWholeLong(f[0], r.frame) || r.frame < 0) {
        r.error = "bad frame K";
        r.frame = -1;
        return r;
    }
    if (!TestHooksDetail::ParseWholeFloat(f[1], r.below) || r.below < 0.0F) {
        r.error = "bad distance D (need finite, 0 <= D <= 4096)";
        r.frame = -1;
        r.below = 0.0F;
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace Game

#endif /* GAME_TESTHOOKS_HPP */
