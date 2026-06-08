#include "combat/EnemyAI06.hpp"

namespace Game {

// FAITHFUL: EnemyAI06__Scout @ game_full.c:677434.
// Gate (lines 677444-677451):
//   cVar1 = dead(0x38); bVar2 = (dead == 0); if (bVar2) cVar1 = dizzy(0xA1);
//   if (!bVar2 || cVar1 != 0) return;   -> return if dead OR dizzy.
// Active body: target_obj = null (0x7C, line 677452), then a tail-call into
// get_transform (line 677454). The re-detect/reflection continuation
// (FUN_007ecec0) is an inlined/indirect jump-table tail using undefined (unaff_)
// registers and is NOT faithfully recoverable -> NOT modelled. No RNG draw in
// the recoverable head.
bool EnemyAI06::Scout() {
    if (m_Dead || m_Dizzy) {
        return false; // gated: dead OR dizzy.
    }
    m_HasTarget = false; // target_obj = null (0x7C, line 677452)
    // owner: get_transform tail (line 677454); re-detect continuation not modelled.
    return true;
}

// FAITHFUL: EnemyAI06__ShootReflection @ game_full.c:677523.
// Gate (lines 677533-677538):
//   cVar1 = dead(0x38); bVar2 = (dead == 0); if (bVar2) cVar1 = dizzy(0xA1);
//   if (bVar2 && cVar1 == 0) { ... }    -> active only when not-dead AND not-dizzy.
// Inside: if (rg_random(0x0C) != 0) rg_random.Range(0, 10) (line 677541), then a
// truncated tail-call (FUN_010b7dcc, line 677544). When rg_random IS null the
// draw is SKIPPED (it is inside the null guard) -> no draw, keeps stream lockstep.
bool EnemyAI06::ShootReflection(int &outRoll) {
    if (m_Dead || m_Dizzy) {
        return false; // gated: dead OR dizzy -> no draw.
    }
    if (!m_HasRng) {
        return false; // rg_random == null (0x0C): draw is inside the guard -> skip.
    }
    outRoll = m_Rng.Range(0, kShootRerollCeiling); // line 677541 (max EXCLUSIVE)
    return true;
}

// FAITHFUL: EnemyAI06__ChildDead @ game_full.c:677601.
// awake = 0 (0x18, line 677608); owner: get_transform tail (line 677610).
void EnemyAI06::ChildDead() {
    m_Awake = false; // 0x18 = 0 (line 677608)
}

// FAITHFUL: EnemyAI06__EndCycle @ game_full.c:677645.
// owner: CancelInvoke(StringLiteral_6549) -- the reflection cadence (line 677655).
// Pure: move_direction = Vector2.zero (0x74, set_move_direction, lines 677661-677662).
// Then if NOT dead (param_1[0xe] == 0, i.e. 0x38 == 0, line 677663) a trailing
// virtual call fires (vtable +0x104, line 677664) -- the target is an owner
// concern; here only the not-dead gate is reported. No RNG.
bool EnemyAI06::EndCycle() {
    // owner: CancelInvoke of the reflection cadence (line 677655).
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // move_direction = zero (0x74)
    if (!m_Dead) {                            // not-dead gate (line 677663)
        // owner: trailing virtual call (vtable +0x104, line 677664).
        return true;
    }
    return false;
}

} // namespace Game
