#include "combat/NpcSummon01.hpp"

namespace Game {

// FAITHFUL: NpcSummon01__ShootReflection @ game_full.c:1671325.
// The rg_random.Range(0, 10) draw (line 1671334, max EXCLUSIVE) is taken
// UNCONDITIONALLY, BEFORE the gate, so a gated-out shot still advances the
// stream. The gate (line 1671335) is `roll < 8 && can_shoot_byte(0x71) != 0`.
// On a pass the decomp clears the can-shoot byte (0x71 = 0, line 1671336),
// null-guards the hand (word 0x22), then the owner schedules
// PetHand.SetAttackTrigger(hand) (line 1671341) + Invoke("Shoot",
// base_cadence @ word 0x1b) (line 1671342). The trailing vtable 0x134
// jumptable re-dispatch (line 1671346, "Could not recover jumptable at
// 0x0119f308") is owner/indirect and NOT modelled. The PetHand call and the
// Invoke scheduling are owner concerns; the only recoverable pure effects are
// the draw, the 0x71 clear, and reporting the base cadence.
NpcSummon01::ShootResult NpcSummon01::ShootReflection(float &outShootCadence,
                                                      float baseCadence) {
    const int roll = m_Rng.Range(0, kShootRollCeiling); // line 1671334 (max EXCL)
    if (roll < kShootRollThreshold && m_CanShoot) {     // line 1671335 gate
        m_CanShoot = false;            // 0x71 = 0 (line 1671336)
        outShootCadence = baseCadence; // owner Invoke("Shoot", word 0x1b @ 1671342)
        return ShootResult::Fired;
    }
    return ShootResult::Held; // draw taken, gate failed: no write
}

// FAITHFUL: NpcSummon01__RunReflection @ game_full.c:1671485.
// After the detect virtual call (vtable 0xe4, line 1671505) the body draws
// rg_random.Range(0, 10) UNCONDITIONALLY (line 1671506, max EXCLUSIVE). The
// detect-result (iVar1 == 1) + roll < 8 branches (lines 1671507-1671520) only
// select which transform the owner reads (get_position on the target at word
// 7, or get_transform on self) for facing -- those are owner reads and not
// modelled. The only recoverable pure effect is the single draw plus the
// move_direction write the tail performs: FUN_00fa16ec(&local,0,0,0) builds a
// zero vector and writes words 0x14/0x15 (bytes 0x50/0x54, lines 1671524-1671525)
// = (0,0). Owner: Invoke("RunReflection", run_cadence @ word 0x1a, line 1671526).
int NpcSummon01::RunReflection() {
    const int roll = m_Rng.Range(0, kWanderRollCeiling); // line 1671506 (max EXCL)
    // detect/roll branches (1671507-1671520) only pick the owner's facing
    // transform; the recoverable tail write is move_direction = (0,0).
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // words 0x14/0x15 (1671524-1671525)
    return roll;
}

// FAITHFUL: NpcSummon01__EndCycle @ game_full.c:1671568.
// The only recoverable state write is move_direction (0x50/0x54) =
// Vector2.zero (Vector2.get_zero @ line 1671584 written to bytes 0x50/0x54,
// lines 1671585-1671586). Owner: CancelInvoke("RunReflection") (line 1671578)
// and anim.SetBool("walk", false) (line 1671592). No RNG.
void NpcSummon01::EndCycle() {
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // move_direction (0x50/0x54) = zero
}

// FAITHFUL: NpcSummon01__GetHurt @ game_full.c:1671668.
// Gate (line 1671675): only a live (byte 0x0d == 0) summon routes the hit to
// UICanvas.GetInstance() (the floating damage number, line 1671677). A dead one
// (byte 0x0d != 0) ignores it. This override touches NO vitals (HP is applied
// by the base damage chain); modelling any HP change here would be fabrication.
// No RNG.
bool NpcSummon01::GetHurt() {
    if (!m_Destroyed) {        // byte 0x0d == 0: alive
        // owner: UICanvas.GetInstance() -> floating damage number (line 1671677).
        return true;           // hit routed
    }
    return false;              // dead: ignored
}

// FAITHFUL: NpcSummon01__OnGameStateChange @ game_full.c:1671726.
// Gated on active (byte 0x0c, line 1671731): when not active the whole body is
// skipped (no write). When active: game_state == 2 (resume, line 1671732)
// clears paused (0x44 = 0, line 1671733) and adds +0.5 to the summon's OWN
// role_attribute.speed_rate (controller field@0x40 + 0x14, line 1671739);
// game_state == 1 (pause, line 1671741) sets paused (0x44 = 1, line 1671742) --
// the get_transform tail there (line 1671744) is owner. Any other game_state
// writes nothing. The decomp reads p+0x40 from the controller itself (param_1)
// -- its own RoleAttribute pointer -- never a master. No RNG.
NpcSummon01::StateChange NpcSummon01::OnGameStateChange(int gameState,
                                                        float &petSpeedRate) {
    if (!m_Active) {                 // byte 0x0c == 0 (line 1671731)
        return StateChange::Ignored; // whole body skipped: no write
    }
    if (gameState == kStateResume) {        // == 2 (line 1671732)
        m_Paused = false;                   // 0x44 = 0 (line 1671733)
        petSpeedRate += kResumeSpeedRateBoost; // (0x40 + 0x14) += 0.5 (line 1671739)
        return StateChange::Resumed;
    }
    if (gameState == kStatePause) { // == 1 (line 1671741)
        m_Paused = true;            // 0x44 = 1 (line 1671742)
        return StateChange::Paused;
    }
    return StateChange::NoChange; // active but game_state not in {1,2}: no write
}

} // namespace Game
