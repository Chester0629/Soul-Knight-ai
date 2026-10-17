#ifndef GAME_GAME_DOORANIM_HPP
#define GAME_GAME_DOORANIM_HPP

#include <algorithm>
#include <string>
#include <vector>

#include "data/GameData.hpp"

/**
 * @file DoorAnim.hpp
 * @brief P2.20: the look of one door piece (d1..d5) over time -- pure sampling of the door_open /
 *        door_close2 clips, no GL, unit-tested (test/DoorAnimTest.cpp).
 *
 * FAITHFUL @ rgprefab/room/room/door_n.prefab + rganim/door_2.controller (bool `open`, default
 * state door_open) + rganim/door_open.anim / door_close2.anim, extracted into room_props.json
 * `door.clips` by tools/build_gamedata.py (build_room_props):
 *   open  (0.2 s): bar `d` local y 1 -> 0.1 over 0.133 s, `shadow` off at 0.133 s, `d` off and the
 *                  base sprite door02 -> door03 at 0.167 s;
 *   close (0.167 s): bar y 0.1 -> 1 over 0.167 s, `d` + `shadow` on and the base door03 -> door02 at
 *                  0.033 s.
 * Float / bool / sprite curves are STEPPED (the value of the last key at or before t, the first
 * key's value before it), the position curve is linearly interpolated -- PORT CHOICE (T-75): the
 * clip's keys carry Hermite tangents the extraction does not keep (the two-key bar curve is the only
 * interpolated one, and its in/out slopes make it linear to within the clip's own tangentMode 1).
 */
namespace Game::DoorAnim {

/// The resolved look of one door piece at one instant.
struct Pose {
    bool baseOpen = false; ///< base sprite is door03 (open) instead of door02 (closed)
    bool barOn = false;    ///< the door01 bar `d` is enabled
    float barY = 0.0F;     ///< bar local y (units above the piece's root)
    bool shadowOn = false; ///< the shadow child is enabled
};

/// Stepped value of @p keys at @p t (first key's value before the first key). 0 for no keys.
inline float Stepped(const std::vector<ClipKey1> &keys, float t) {
    if (keys.empty()) {
        return 0.0F;
    }
    float v = keys.front().v;
    for (const ClipKey1 &k : keys) {
        if (k.t <= t) {
            v = k.v;
        }
    }
    return v;
}

/// Linearly interpolated value of @p keys at @p t (clamped to the first / last key). 0 for no keys.
inline float Linear(const std::vector<ClipKey1> &keys, float t) {
    if (keys.empty()) {
        return 0.0F;
    }
    if (t <= keys.front().t) {
        return keys.front().v;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].t) {
            const ClipKey1 &a = keys[i - 1];
            const ClipKey1 &b = keys[i];
            const float span = b.t - a.t;
            return span <= 0.0F ? b.v : a.v + (b.v - a.v) * ((t - a.t) / span);
        }
    }
    return keys.back().v;
}

/// Sprite stem active at @p t (stepped); "" for no keys.
inline const std::string &SpriteAt(const std::vector<ClipSpriteKey> &keys, float t) {
    static const std::string kEmpty;
    if (keys.empty()) {
        return kEmpty;
    }
    const std::string *s = &keys.front().sprite;
    for (const ClipSpriteKey &k : keys) {
        if (k.t <= t) {
            s = &k.sprite;
        }
    }
    return *s;
}

/// The pose @p t seconds after the @p clip started (t clamped to [0, stop]); @p openSprite is the
/// base sprite stem that means "open" (door03).
inline Pose Sample(const DoorClipDef &clip, const std::string &openSprite, float t) {
    const float tt = std::clamp(t, 0.0F, clip.stop);
    Pose p;
    p.baseOpen = SpriteAt(clip.baseSprite, tt) == openSprite;
    p.barOn = Stepped(clip.barEnabled, tt) > 0.5F;
    p.barY = Linear(clip.barY, tt);
    p.shadowOn = Stepped(clip.shadowEnabled, tt) > 0.5F;
    return p;
}

/// Sample the door in @p opening (true: door_open, false: door_close2) at @p t seconds since the last
/// state change.
inline Pose Sample(const DoorPropsDef &door, bool opening, float t) {
    return Sample(opening ? door.open : door.close, door.openSprite, t);
}

} // namespace Game::DoorAnim

#endif /* GAME_GAME_DOORANIM_HPP */
