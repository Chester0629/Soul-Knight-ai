#include "combat/RGBDelayDivision.hpp"

namespace Game {

// FAITHFUL: RGBDelayDivision__AdjustAngle guard @ game_full.c:468175 and
// RGBDelayDivision__OnTaken @ game_full.c:468196. AdjustAngle returns early when
// `c_angle * c_count < 0x169` (361); OnTaken acts when `0x168 < c_angle * c_count`.
// Both pivot on the product reaching 361, i.e. strictly past a full turn (360) plus
// one. This returns that "the fan would overfill the circle" predicate.
bool RGBDelayDivision::ShouldClampAngle(int angle, int count) {
    return (angle * count) >= kOverfillThreshold;
}

// FAITHFUL: RGBDelayDivision__AdjustAngle @ game_full.c:468178-468179 (and the same
// two lines in OnTaken @ 468197-468198):
//   if (c_angle * c_count < 361) return;          // leave c_angle untouched
//   c_angle = __aeabi_idiv(360, c_count);         // 0x168 / count, integer divide
// The pass-through path deliberately does NOT write c_angle (the decomp's early
// return), so the original value is returned unchanged when no clamp is needed.
int RGBDelayDivision::ClampAngle(int angle, int count) {
    if (!ShouldClampAngle(angle, count)) {
        return angle; // early return: field 0x50 is not written
    }
    if (count == 0) {
        return angle; // guard the divide; count==0 cannot satisfy the >=361 gate
    }
    return kFullCircleDegrees / count; // 360 / count, integer (truncating) divide
}

// FAITHFUL: RGBDelayDivision__FixedUpdate @ game_full.c:468269-468276.
//   bVar2 = awake;                       // 0x20 != 0
//   iVar1 = bVar2 ? rotate_angle : 0;    // 0x1c
//   if (!bVar2 || iVar1 == 0) return;    // bail unless awake AND rotate_angle != 0
//   get_transform(...);                  // owner rotation
bool RGBDelayDivision::ShouldRotate(bool awake, int rotateAngle) {
    return awake && (rotateAngle != 0);
}

// FAITHFUL: Division MoveNext dispatch header @ game_full.c:468312-468319.
//   uVar2 = state; state = -1; if (uVar2 < 3) disp = uVar2 + 3; else disp = 0;
//   disp 3 -> Wait, 4 -> Spawn, 5 -> Finalise; else no-op (Done).
// The `uVar2 < 3` is an UNSIGNED compare, so Done (-1 / 0xffffffff) is NOT < 3 and
// maps to disp 0 -> Done, exactly as the decomp leaves the machine.
RGBDelayDivision::State RGBDelayDivision::Dispatch(State stored) {
    const int s = static_cast<int>(stored);
    const int disp = (s >= 0 && s < 3) ? (s + 3) : 0;
    switch (disp) {
    case 3:
        return State::Wait;
    case 4:
        return State::Spawn;
    case 5:
        return State::Finalise;
    default:
        return State::Done; // unmatched dispatch -> MoveNext yields nothing
    }
}

// FAITHFUL: Division MoveNext spawn block @ game_full.c:468348-468355.
//   uVar2 = c_count;
//   if ((uVar2 & 1) == 0) start = -(count / 2);        // even
//   else                  start = -((count - 1) / 2);  // odd
// Returns the signed leftmost index; child i (start..) fires at base + i*c_angle.
int RGBDelayDivision::FanStartIndex(int count) {
    if ((count & 1) == 0) {
        return -(count / 2); // even fan: symmetric about a gap
    }
    return -((count - 1) / 2); // odd fan: symmetric about the centre bullet
}

// FAITHFUL: Division MoveNext spawn entry. Latches the fan size and seeds the walk
// at FanStartIndex(count). The PlayEffect(c_audio_clip @ 0x48) and the per-child
// Instantiate(c_bullet @ 0x5c) / transform are owner side effects.
void RGBDelayDivision::BeginSpawn(int count) {
    m_Remaining = (count > 0) ? count : 0;
    m_NextIndex = FanStartIndex(count);
    m_State = (m_Remaining > 0) ? State::Spawn : State::Done;
}

// FAITHFUL: the fan iteration implied by the spawn block @ game_full.c:468343-468373
// (`if (0 < c_count) { ... }` then walk the children). Emits each child index in
// turn (start, start+1, ...) for `count` steps; each index multiplies c_angle to
// give that child's angular offset. The Instantiate + rotation per child is owner.
bool RGBDelayDivision::SpawnStep(int &outIndex) {
    if (m_Remaining <= 0) {
        m_State = State::Done;
        return false;
    }
    outIndex = m_NextIndex;
    ++m_NextIndex;
    --m_Remaining;
    if (m_Remaining == 0) {
        m_State = State::Done;
    }
    return true;
}

} // namespace Game
