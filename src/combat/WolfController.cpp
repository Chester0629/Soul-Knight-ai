#include "combat/WolfController.hpp"

namespace Game {

// FAITHFUL: WolfController__RunReflection @ game_full.c:626893.
// Pure recoverable logic: the single rg_random.Range(0, 10) draw (line 626912,
// max EXCLUSIVE; rg_random @ param_1[0x24] == field 0x90, null-guard line 626907).
// The virtual call (vtable 0xe4, line 626902) -> if(ret==1) get_transform
// (line 626905) is the facing/detect decision (indirect/owner) and the
// get_position tail is owner -> not modelled.
int WolfController::RunReflection() {
    m_LastWanderRoll = m_Rng.Range(0, kWanderRerollCeiling); // line 626912 (max EXCL)
    return m_LastWanderRoll;
}

// FAITHFUL: WolfController__Scout @ game_full.c:626854.
// The only state write the decomp performs is target_obj = master_tf
// (param_1[7] = param_1[0x1d], line 626862): the pet targets its master. Then it
// dispatches FixedRotation (owner facing, line 626867) and RunReflection
// (line 626868). Scout takes no draw of its own; RunReflection takes the single
// Range(0,10). The virtual detect call (vtable 0xec, line 626861) and the
// param_1[0x11]==1 -> get_transform tail (lines 626863-626865) are
// indirect/owner and are not modelled.
int WolfController::Scout() {
    m_TargetIsMaster = true; // target_obj = master_tf (0x1C = 0x74)
    // owner: FixedRotation() facing toward target (line 626867).
    return RunReflection();  // line 626868 (carries the one rg_random draw)
}

// FAITHFUL: WolfController__EndCycle @ game_full.c:626917.
// The ONLY state write is move_direction = Vector2.zero (0x50/0x54, lines
// 626934-626935). The decomp does NOT touch target_obj (0x1C), so the
// targets-master latch is left UNCHANGED. The Wolf override has NO dead-gate.
// Owner: CancelInvoke("Scout") (line 626927) and anim.SetBool("walk", false)
// (line 626941). No RNG.
void WolfController::EndCycle() {
    m_MoveDirZeroed = true; // move_direction (0x50/0x54) = Vector2.zero
}

// FAITHFUL: WolfController__OnAtk @ game_full.c:626948.
// prefab = (strengthen(0x80) == 0) ? bullet1(0x84) : bullet2(0x88)
// (lines 626958-626973). Owner: Instantiate<RGWeapon>(prefab) +
// GetComponent<RGSword> (lines 626974-626980). No RNG.
WolfController::AtkPrefab WolfController::OnAtk() const {
    return m_Strengthen ? AtkPrefab::Bullet2 : AtkPrefab::Bullet1;
}

} // namespace Game
