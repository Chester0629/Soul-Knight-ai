#include "combat/NpcMercenaryController.hpp"

namespace Game {

// FAITHFUL: NpcMercenaryController__ShootReflection @ game_full.c:1670499.
// Pure dispatch on the melee flag (byte 0xa8, line 1670502): set ->
// MeleeShootReflection (line 1670503), clear -> RemoteShootReflection
// (line 1670506). No draw, no write of its own; the result/out-params flow
// through from the branch it forwards to.
NpcMercenaryController::ShootResult NpcMercenaryController::ShootReflection(
    float &outCadence, float baseCadence, int itemLevel, ShootBranch &outBranch) {
    if (m_Melee) { // byte 0xa8 != 0
        outBranch = ShootBranch::Melee;
        return MeleeShootReflection(outCadence, baseCadence, itemLevel);
    }
    outBranch = ShootBranch::Remote;
    return RemoteShootReflection(outCadence, baseCadence, itemLevel);
}

// FAITHFUL: NpcMercenaryController__MeleeShootReflection @ game_full.c:1670647.
// The Range(0,10) draw is UNCONDITIONAL (line 1670659, max EXCL) and precedes
// the gate, so a gated-out shot still advances the stream. Gate (line 1670660):
// roll < 8 && can-shoot byte (0x71) != 0. On a pass clear 0x71 (line 1670661);
// owner schedules Invoke("Shoot", base(0x1b) + itemLevel*0.25) (line 1670695).
// The RGWeapon.GetItemLevel read, PetHand.SetAttackTrigger and the vtable-0x134
// jumptable re-dispatch are owner and not modelled.
NpcMercenaryController::ShootResult NpcMercenaryController::MeleeShootReflection(
    float &outCadence, float baseCadence, int itemLevel) {
    m_LastRoll = m_Rng.Range(0, kRollCeiling); // line 1670659 (max EXCL)
    if (m_LastRoll < kRollThreshold && m_CanShoot) { // line 1670660
        m_CanShoot = false; // 0x71 = 0 (line 1670661)
        // line 1670695: cadence = base + itemLevel * 0.25
        outCadence = baseCadence + static_cast<float>(itemLevel) * kMeleeItemLevelStep;
        return ShootResult::Fired;
    }
    return ShootResult::Held;
}

// FAITHFUL: NpcMercenaryController__RemoteShootReflection @ game_full.c:1670707.
// The Range(0,10) draw is UNCONDITIONAL (line 1670721, max EXCL). The gate is
// written as `(7 < roll) || (0x71 == 0) -> skip` (line 1670722), i.e. proceed
// only when roll < 8 && can-shoot (0x71) != 0. On a pass clear 0x71
// (line 1670723); owner schedules Invoke("Shoot", base(0x1b) *
// (itemLevel*0.3 + 1.0)) (line 1670804). The interior weapon-type RTTI branch
// (GunShield / Gun011 / Gun007, lines 1670728-1670800) only selects which
// PetHand call the owner makes -- it takes no draw and is owner; not modelled.
// The vtable-0x134 jumptable re-dispatch (line 1670808) is owner.
NpcMercenaryController::ShootResult NpcMercenaryController::RemoteShootReflection(
    float &outCadence, float baseCadence, int itemLevel) {
    m_LastRoll = m_Rng.Range(0, kRollCeiling); // line 1670721 (max EXCL)
    if (m_LastRoll < kRollThreshold && m_CanShoot) { // line 1670722 (negated)
        m_CanShoot = false; // 0x71 = 0 (line 1670723)
        // line 1670804: cadence = base * (itemLevel * 0.3 + 1.0)
        outCadence = baseCadence *
                     (static_cast<float>(itemLevel) * kRemoteItemLevelStep + kRemoteCadenceBias);
        return ShootResult::Fired;
    }
    return ShootResult::Held;
}

// FAITHFUL: NpcMercenaryController__RemoteRunReflection @ game_full.c:1670550.
// After the detect virtual call (vtable 0xe4, line 1670569) the Range(0,10) draw
// is UNCONDITIONAL (line 1670578, max EXCL). The detect-result + roll<8 branches
// (lines 1670570/1670579) only select which transform the owner reads for facing
// -- owner reads, not modelled. The recoverable effect is the single draw plus
// move_direction (words 0x14/0x15 == 0x50/0x54, lines 1670586-1670587) =
// Vector2.zero, and the run cadence (word 0x1a) the owner passes to
// Invoke("RunReflection", ...) (line 1670588).
int NpcMercenaryController::RemoteRunReflection(float &outRunCadence, float runCadence) {
    m_LastRoll = m_Rng.Range(0, kRollCeiling); // line 1670578 (max EXCL)
    m_MoveDirection = glm::vec2(0.0F, 0.0F);    // 0x50/0x54 = Vector2.zero
    outRunCadence = runCadence;                 // word 0x1a (owner Invoke delay)
    return m_LastRoll;
}

// FAITHFUL: NpcMercenaryController__MeleeScout @ game_full.c:1670512.
// The only state write is target_obj (word 7 == 0x1c) = the owner ref
// (word 0x1d == 0x74, line 1670520). The detect virtual (vtable 0xec,
// line 1670519), the conditional get_transform (line 1670523) and the
// run-reflection re-dispatch (vtable 0x12c, line 1670525) are owner. No draw.
void NpcMercenaryController::MeleeScout() {
    m_TargetIsOwner = true; // target_obj (0x1c) = owner ref (0x74)
}

// FAITHFUL: NpcMercenaryController__RemoteScout @ game_full.c:1670531.
// Structurally identical to MeleeScout: the only state write is target_obj
// (0x1c) = the owner ref (0x74, line 1670539); detect/get_transform/re-dispatch
// are owner. No draw.
void NpcMercenaryController::RemoteScout() {
    m_TargetIsOwner = true; // target_obj (0x1c) = owner ref (0x74)
}

// FAITHFUL: NpcMercenaryController__EndCycle @ game_full.c:1670814.
// The only recoverable write is move_direction (bytes 0x50/0x54) = Vector2.zero
// (lines 1670830-1670832). Owner: CancelInvoke("RunReflection") (line 1670824)
// and anim.SetBool("walk", false) (line 1670838). No dead-gate, no RNG.
void NpcMercenaryController::EndCycle() {
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // 0x50/0x54 = Vector2.zero
}

// FAITHFUL: NpcMercenaryController__GetHurt @ game_full.c:1670954.
// Gate (line 1670961): only a live (byte 0x0d == 0) mercenary routes the hit to
// UICanvas.GetInstance() (line 1670963, the floating damage number). A dead one
// ignores it. Touches NO vitals (the base damage chain owns HP). No RNG.
bool NpcMercenaryController::GetHurt() const {
    return !m_Dead; // byte 0x0d == 0 -> route to UICanvas
}

// FAITHFUL: NpcMercenaryController__Dead @ game_full.c:1670972.
// Recoverable gate (line 1670979): if already dead (byte 0x0d != 0) the body is
// skipped. This override never WRITES 0x0d (the base chain owns the latch), so
// we model only the gate, not a state write. The rest (anim.SetTrigger("dead")
// gated on byte 0x85 @ line 1670982, the get_transform spawn @ line 1670992) is
// owner. No RNG.
bool NpcMercenaryController::Dead() const {
    return !m_Dead; // byte 0x0d != 0 -> early return (owner body skipped)
}

} // namespace Game
