#include "combat/BulletRoundabout.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: BulletRoundabout AdjustmentAngle wrap block
// (FUN_00afe978 @ game_full.c:963816). The decomp keeps delta unchanged while
// |delta| < 180 (and at exactly 180, per the `< 180.0` compare), otherwise pulls
// it back across a full turn: delta += (delta < 0 ? +360 : -360). This is the
// standard Mathf.DeltaAngle reduction into (-180, 180].
float BulletRoundabout::WrapDeltaAngle(float delta) {
    if (std::fabs(delta) > kHalfTurnDegrees) {
        delta += (delta < 0.0F) ? kFullTurnDegrees : -kFullTurnDegrees;
    }
    return delta;
}

// FAITHFUL: BulletRoundabout AdjustmentAngle step/snap branch @ game_full.c:963875.
// step (angle_speed, 0x60) vs |delta|:
//   if (step <= |delta|)  move_angle += (delta <= 0 ? +step : -step);  // one step
//   else                  move_angle -= delta;                          // snap
// The (delta <= 0)/(delta > 0) split is the recovered Z||N flag test
// `(bVar1>>6 & 1) || (bVar1>>7) != NAN(param_1)` i.e. (delta == 0) || (delta < 0).
float BulletRoundabout::StepMoveAngle(float currentMoveAngle, float targetAngle,
                                      float angleSpeed) {
    const float delta = WrapDeltaAngle(targetAngle - currentMoveAngle);
    if (angleSpeed <= std::fabs(delta)) {
        // Step by angle_speed; direction is the negation of delta's sign.
        return (delta <= 0.0F) ? (currentMoveAngle + angleSpeed)
                               : (currentMoveAngle - angleSpeed);
    }
    // Snap: close the remaining gap (move_angle -= delta), exactly as the decomp's
    // else-branch `*(move_angle) = *(move_angle) - param_1`.
    return currentMoveAngle - delta;
}

// FAITHFUL: MoveNext dispatch header @ game_full.c:963665.
//   s = state; state = -1; disp = (s < 3) ? s + 3 : 0;
//   disp 3 -> Entry, 4 -> Spin, 5 -> Tail; else the `if (disp != 5) return 0;`
//   guard makes it a no-op (Done).
BulletRoundabout::State BulletRoundabout::Dispatch(State stored) {
    const int s = static_cast<int>(stored);
    const int disp = (s >= 0 && s < 3) ? (s + 3) : 0;
    switch (disp) {
    case 3:
        return State::Entry;
    case 4:
        return State::Spin;
    case 5:
        return State::Tail;
    default:
        return State::Done; // unmatched dispatch -> MoveNext returns 0 (no yield)
    }
}

// FAITHFUL: MoveNext entry block @ game_full.c:963672. Copies the spin repeat
// count (this+0xc) into the iterator counter (0x8) and arms the spin loop. The
// active-flag clear (this+0x68 = 0) and the `if (delay(0x48) > 0) WaitForSeconds`
// yield are owner side effects.
void BulletRoundabout::BeginSpin(float spinCount) {
    m_Counter = spinCount;
    m_State = State::Spin;
}

// FAITHFUL: MoveNext spin countdown loop @ game_full.c:963709.
//   while (counter > 0) { <Vector2 velocity multiply -- owner>; counter -= 1.0; }
//   then comp+0x68 = 1 (owner).
// Returns true (and decrements) for each iteration the loop body runs; returns
// false once exhausted, advancing the machine to Done.
bool BulletRoundabout::SpinStep() {
    if (m_Counter > 0.0F) {
        m_Counter -= 1.0F;
        return true;
    }
    m_State = State::Done;
    return false;
}

} // namespace Game
