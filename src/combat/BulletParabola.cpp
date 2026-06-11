#include "combat/BulletParabola.hpp"

namespace Game {

// FAITHFUL: BulletParabola__FixedUpdate gate @ game_full.c:963514-963519.
//   bVar3 = *(char *)(this + 0x20) != '\0';     // active flag
//   iVar1 = 0; if (bVar3) iVar1 = *(int *)(this + 0x1c);  // counter (active only)
//   if (!bVar3 || iVar1 == 0) { <accumulate>; }
// The accumulator block runs when the bullet is NOT active, or is active with a
// zero counter. When !active the decomp forces the counter to 0 first, but the
// `!active` term already makes the gate true, so the counter value is irrelevant
// in that arm -- mirrored here by only consulting `counter` under `active`.
bool BulletParabola::ShouldAccumulate(bool active, int counter) {
    return !active || counter == 0;
}

// FAITHFUL: BulletParabola__FixedUpdate accumulator @ game_full.c:963520-963522.
//   fVar4 = *(float *)(this + 0x4c);                      // rate
//   fVar2 = Time.fixedDeltaTime;                          // owner-driven dt
//   *(float *)(this + 0x54) = *(float *)(this + 0x54) + fVar4 * fVar2;
// dt is supplied by the caller (the owner's Time.fixedDeltaTime). The trailing
// get_transform that consumes this progress as an arc position is OWNER-side and
// is not performed here. The accumulator only runs behind the FixedUpdate gate.
float BulletParabola::Tick(float dt) {
    if (ShouldAccumulate(m_Active, m_Counter)) {
        m_Progress += m_Rate * dt;
    }
    return m_Progress;
}

} // namespace Game
