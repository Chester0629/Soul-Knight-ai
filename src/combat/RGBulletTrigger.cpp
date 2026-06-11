#include "combat/RGBulletTrigger.hpp"

namespace Game {

// FAITHFUL: RGBulletTrigger__set_through_count @ game_full.c:467550.
//   *(this+0x18) = param_2;          // through_count
//   *(this+0x14) = 0 < param_2;      // can_through derived from the budget
// The consumer at game_full.c:390729-390731 re-reads get_through_count and
// re-stores the same `can_through = (0 < count)`, confirming the invariant.
void RGBulletTrigger::SetThroughCount(int value) {
    m_ThroughCount = value;
    m_CanThrough = 0 < value;
}

// FAITHFUL: RGBulletTrigger__SetInfo(bool) @ game_full.c:469757.
//   uVar1 = 0; if (param_4 != 0) uVar1 = 0xff;
// param_4 is the can_through bool; 0xff is the infinite-pierce sentinel budget.
int RGBulletTrigger::ThroughBudgetFromBool(bool canThrough) {
    return canThrough ? kInfinitePierceBudget : 0;
}

// FAITHFUL: composes SetInfo(bool)'s budget conversion (game_full.c:469757) with
// set_through_count's state writes (game_full.c:467550). The decomp's SetInfo(bool)
// forwards through vtable slot 0xe4 into the (int) overload, which stores the
// budget; the net effect on through_count / can_through is reproduced here.
void RGBulletTrigger::ApplySetInfoThrough(bool canThrough) {
    SetThroughCount(ThroughBudgetFromBool(canThrough));
}

// FAITHFUL: RGBulletTrigger__GetDamageFactor @ game_full.c:469729.
//   if (*(this+0x40) != 0) { ... Singleton<RGGameProcess> ... (truncates) }
//   return 0x3f800000;   // 1.0f
// The has_ice_buff branch tail-calls the singleton getter and the decomp cannot
// recover what factor it returns, so only the recovered non-ice 1.0f is modelled.
// When hasIceBuff is true the real factor is decided owner-side; we return the
// recovered baseline (1.0f) rather than fabricate the ice value (see TakesIceBranch
// and kIceBuffFactorUnverified for the unrecoverable branch).
float RGBulletTrigger::GetDamageFactor(bool hasIceBuff) {
    // hasIceBuff true selects the truncated owner-side ice branch (see
    // TakesIceBranch); its factor is unrecoverable, so every recovered path
    // resolves to the single recovered `return 0x3f800000` (1.0f).
    (void)hasIceBuff;
    return kNoBuffDamageFactor;
}

// FAITHFUL: RGBulletTrigger__OnTriggerEnter2D @ game_full.c:469058.
//   *(this+0xc) = 0;   // need_destory = false
// This is the only recovered statement; the body then null-checks the collider
// and tail-calls Component::get_gameObject (truncation = owner). The per-tag
// damage dispatch is entirely owner-side.
void RGBulletTrigger::OnTriggerEnterHead() { m_NeedDestroy = false; }

} // namespace Game
