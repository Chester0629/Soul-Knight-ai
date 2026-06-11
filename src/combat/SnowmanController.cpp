#include "combat/SnowmanController.hpp"

namespace Game {

// FAITHFUL: SnowmanController__ShootReflection @ game_full.c:1652669.
// Gate (line 1652676): if dead/destroyed (field 0x0d) -> return, no write.
// When live: byte field 0x71 = 0 (line 1652679) -- the only recoverable state
// write. The rest is owner-side: Invoke("OnAtk", hand@0x6c) (line 1652680),
// anim.SetTrigger("tap_atk") (line 1652685) and SetTrigger(...) (line 1652690).
// No rg_random draws.
bool SnowmanController::ShootReflection() {
    if (m_Destroyed) {
        return false; // 0x0d gate: dead snowman does not fire.
    }
    m_ReArmFlag = false; // byte 0x71 = 0 (line 1652679)
    // owner: Invoke("OnAtk", hand@0x6c); anim.SetTrigger("tap_atk"); SetTrigger().
    return true;
}

// FAITHFUL: SnowmanController__Scout @ game_full.c:1652696.
// First a virtual dispatch (line 1652703, owner), then target(0x1c) = master
// (0x74) (line 1652704). Then the paused gate (field 0x44 == 1, line 1652705):
// when paused the body bails to a transform read and the downstream cadence
// dispatch (line 1652709) does NOT run. Pure part: link the target from the
// master and report whether the cadence dispatch would run. No rg_random draws.
bool SnowmanController::Scout(bool hasMaster) {
    m_HasMaster = hasMaster;
    m_HasTarget = hasMaster; // target(0x1c) = master(0x74) (line 1652704)
    if (m_Paused) {          // 0x44 == 1 (line 1652705): bail, no cadence dispatch.
        return false;
    }
    // owner: FixedRotation + shoot-cadence virtual dispatch (line 1652709).
    return true;
}

// FAITHFUL: SnowmanController__GetHurt @ game_full.c:1652826 (override of
// RGPetController__GetHurt). Gate (line 1652833): only a live (field 0x0d == 0)
// snowman routes the hit to UICanvas (the floating damage number, line 1652835);
// a dead one ignores it. This override touches NO vitals -- modelling any HP
// change here would be fabrication. No rg_random draws.
bool SnowmanController::GetHurt() {
    if (m_Destroyed) {
        return false; // 0x0d gate: dead snowman ignores the hit.
    }
    // owner: UICanvas.GetInstance()...ShowTextHurt (floating damage number).
    return true;
}

// FAITHFUL: SnowmanController__OnGameStateChange @ game_full.c:1652881.
// Gated on active (field 0x0c, line 1652886): not active -> no write at all.
// When active: game_state == 2 (resume) clears paused (0x44 = 0, line 1652888)
// and adds +0.5 to the pet's OWN role_attribute.speed_rate (controller
// field@0x40 + 0x14, line 1652894); game_state == 1 (pause) sets paused
// (0x44 = 1, line 1652897). Any other game_state writes nothing. The decomp
// loads iVar1 = *(int *)(param_1 + 0x40) from the controller itself (param_1)
// -- the same RoleAttribute get_attribute@427175 returns and FixedUpdate@427251
// reads as the pet's speed multiplier -- never the master pointer at 0x74.
// No rg_random draws.
SnowmanController::StateChange
SnowmanController::OnGameStateChange(int gameState, float &petSpeedRate) {
    if (!m_Active) {
        return StateChange::Ignored; // 0x0c gate (line 1652886): nothing happens.
    }
    if (gameState == kStateResume) {       // param_2 == 2 (line 1652887)
        m_Paused = false;                  // 0x44 = 0 (line 1652888)
        petSpeedRate += kResumeSpeedRateBoost; // +0.5 (line 1652894)
        return StateChange::Resumed;
    }
    if (gameState == kStatePause) { // param_2 == 1 (line 1652896)
        m_Paused = true;            // 0x44 = 1 (line 1652897)
        return StateChange::Paused;
    }
    return StateChange::NoChange; // active but game_state not in {1,2}: no write.
}

} // namespace Game
