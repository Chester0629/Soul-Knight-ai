#include "combat/RGBatteryController.hpp"

namespace Game {

// FAITHFUL: RGBatteryController__ShootReflection @ game_full.c:467947.
// Detect-gate (line 467958): proceed only when can_shoot (byte 0x71) != 0 AND
// is_charging (word 0x26 == byte 0x98) == 0. On a pass: clear can_shoot
// (0x71 = 0, line 467959) and set is_charging (0x98 = 1, line 467960). Then the
// owner reads rigibody (0x18, line 467961), zeroes its velocity (Vector2.zero,
// lines 467962-467972) and PetHand.SetAttack(hand 0x8c, 1) (line 467977 site).
// The cadence: Invoke("StopShooting", shoot_duration 0x90 == word 0x24)
// (line 467978) -- reported via outStopDelay for the owner to schedule. If
// rocket_mode (byte 0x80 == word 0x20) is set (line 467979), CreateRocket also
// runs (line 467980). The trailing vtable-0x134 re-dispatch (line 467983) is
// owner/indirect and is NOT modelled. No rg_random draws.
RGBatteryController::FireResult
RGBatteryController::ShootReflection(float &outStopDelay, float shootDuration) {
    if (!m_CanShoot || m_IsCharging) { // line 467958: !can_shoot(0x71) || is_charging(0x98)
        return FireResult::Gated;      // charge block skipped: no write, no scheduled stop.
    }
    m_CanShoot = false;        // 0x71 = 0 (line 467959)
    m_IsCharging = true;       // 0x98 = 1 (line 467960)
    // owner: rigibody(0x18).velocity = Vector2.zero; PetHand.SetAttack(hand 0x8c, 1).
    outStopDelay = shootDuration; // Invoke("StopShooting", shoot_duration 0x90) (line 467978)
    if (m_RocketMode) {        // (char)param_1[0x20] != 0 (line 467979)
        return FireResult::Rocket; // CreateRocket also runs (line 467980).
    }
    return FireResult::Charged;
}

// FAITHFUL: RGBatteryController__StopShooting @ game_full.c:468033.
// Owner first CancelInvoke("StopShooting") (line 468040). The only state write
// is is_charging (byte 0x98 = 0, line 468041) -- the inverse of ShootReflection's
// latch. Owner: PetHand.SetAttack(hand 0x8c, 0) (line 468046). The cadence:
// Invoke("ShootReflection", shoot-cadence 0x6c) (line 468047) re-arms the next
// fire -- reported via outReInvokeDelay. No gate, no rg_random draws.
void RGBatteryController::StopShooting(float &outReInvokeDelay, float shootCadence) {
    // owner: CancelInvoke("StopShooting") (line 468040).
    m_IsCharging = false;            // 0x98 = 0 (line 468041)
    // owner: PetHand.SetAttack(hand 0x8c, 0) (line 468046).
    outReInvokeDelay = shootCadence; // Invoke("ShootReflection", 0x6c) (line 468047)
}

// FAITHFUL: RGBatteryController__CreateRocket @ game_full.c:467989.
// Owner: the detect virtual (vtable 0xe4, line 467998) -> on a hit (iVar1 == 1),
// the PrefabPool.get_Inst rocket spawn (lines 468000-468006) -- tail-call-
// truncated, NOT modelled. The recoverable HEAD: when is_charging (byte 0x98 ==
// word 0x26) is still set (line 468008), re-Invoke("...", 1.0f) (line 468009) to
// keep the rocket cadence going. We report whether that re-Invoke would fire and
// at what fixed delay (kRocketReInvokeDelay == 1.0f). No rg_random draws.
bool RGBatteryController::CreateRocket(float &outReInvokeDelay) const {
    // owner: detect virtual (vtable 0xe4) -> PrefabPool rocket spawn on a hit.
    if (m_IsCharging) {                       // (char)param_1[0x26] != 0 (line 468008)
        outReInvokeDelay = kRocketReInvokeDelay; // Invoke("...", 0x3f800000 == 1.0f) (line 468009)
        return true;
    }
    return false;
}

// FAITHFUL: RGBatteryController__EndCycle @ game_full.c:468053.
// The only state write is move_direction = Vector2.zero (0x50, lines 468069-468070;
// word index param_1[0x14]/[0x15]). Owner: the vtable-0x124 re-dispatch
// (line 468071). No dead-gate, no rg_random draws.
void RGBatteryController::EndCycle() {
    m_MoveDirection = glm::vec2(0.0F, 0.0F); // move_direction (0x50) = Vector2.zero
    // owner: vtable-0x124 re-dispatch (line 468071).
}

// FAITHFUL: RGBatteryController__GetHurt @ game_full.c:468077.
// Gate (line 468084): only a live (byte 0x0d == 0) battery routes the hit to
// UICanvas.GetInstance() (the floating damage number, line 468086). A dead one
// ignores it. This override touches NO vitals -- modelling any HP change here
// would be fabrication. No rg_random draws.
bool RGBatteryController::GetHurt() const {
    if (m_Dead) {
        return false; // 0x0d gate: dead battery ignores the hit.
    }
    // owner: UICanvas.GetInstance()... (floating damage number, line 468086).
    return true;
}

} // namespace Game
