#include "combat/BossAI06Child.hpp"

namespace Game {

// FAITHFUL: BossAI06Child__GetToTarget @ game_full.c:441672.
//   if (free_move(0x28) != 0) { value = UnityEngine.Random.Range(0, 2);
//                               Attack(value); get_transform(...); }
//   get_transform(...);   // owner-side move toward target either way
// The Range(0, 2) draw is the GLOBAL Unity RNG (max EXCLUSIVE -> {0, 1}),
// modelled here by the injected RGRandom (same Xorshift128, same int semantics).
// Gated out (free_move == false) => NO draw, keeping the stream in lockstep.
int BossAI06Child::GetToTarget() {
    if (!m_FreeMove) {
        return kAttackNone; // gated path: consumes no draw, dispatches no attack
    }
    const int value = m_Rng.Range(0, kSelectCeiling); // {0, 1}; never selects Atk03
    return Attack(value);
}

// FAITHFUL: BossAI06Child__Attack @ game_full.c:441486 (pure dispatch, no RNG).
//   value == 2 -> Atk03 ; value == 1 -> Atk02 ; value == 0 -> Atk1 ; else return.
// Atk1 carries the only brain-modelled field write (lock_target = 0); Atk02/Atk03
// are wholly owner-side (PrefabPool / Instantiate / animator).
int BossAI06Child::Attack(int value) {
    if (value == kAttackAtk03) {
        return kAttackAtk03; // owner: Atk03 -> Instantiate(bullet03 @0x3c)
    }
    if (value == kAttackAtk02) {
        return kAttackAtk02; // owner: Atk02 -> PrefabPool bullet02 spread
    }
    if (value == kAttackAtk1) {
        Atk1();              // lock_target(0x2b) = 0 (modelled write)
        return kAttackAtk1;  // owner tail: destroy the_aim(0x40), re-spawn aim
    }
    return kAttackNone; // value not in {0,1,2}: Attack() falls through, no-op
}

// FAITHFUL: BossAI06Child__Atk1 @ game_full.c:441790.
//   lock_target(0x2b) = 0;  (then owner-side Destroy + PrefabPool spawn)
void BossAI06Child::Atk1() {
    m_LockTarget = false;
}

// FAITHFUL: BossAI06Child__FindTarget @ game_full.c:441745.
//   has_target(0x2a) = 0;  get_transform(...);   // owner-side detect/move
void BossAI06Child::FindTarget() {
    m_HasTarget = false;
}

// FAITHFUL: BossAI06Child__EndAtk03 @ game_full.c:441925.
//   in_atk3(0x48) = 0;  get_transform(...);       // owner-side
void BossAI06Child::EndAtk03() {
    m_InAtk3 = false;
}

// FAITHFUL: BossAI06Child___ctor @ game_full.c:441623.
//   get_target(0x29) = 1; lock_target(0x2b) = 1; speed(0x2c) = 10.0f (0x41200000).
// (ctor's tail is a RaycastHit2D[] type-init via FUN_010798e8 -> owner/runtime.)
void BossAI06Child::ResetToCtorState() {
    m_GetTarget = kCtorGetTarget;   // 0x29 = 1
    m_LockTarget = kCtorLockTarget; // 0x2b = 1
    m_Speed = kCtorSpeed;           // 0x2c = 10.0f
}

} // namespace Game
