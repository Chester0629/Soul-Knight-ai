#ifndef GAME_SIM_SIMCONFIG_HPP
#define GAME_SIM_SIMCONFIG_HPP

#include "game/WorldScale.hpp"

namespace Game::Sim {

/// The simulation's fixed timestep, matching Unity's default FixedUpdate (0.02s).
/// All brains + cadence advance in whole multiples of this; the runtime is
/// seed-reproducible because nothing reads wall-clock time.
inline constexpr float kFixedStepSeconds = 0.02F;

/// The fixed timestep in milliseconds (the unit GameScene passes as dtMs).
inline constexpr float kFixedStepMs = 20.0F;

/// Upper bound on fixed steps run for one Advance() call, so a huge frame hitch
/// (e.g. a debugger pause) cannot spiral into thousands of catch-up steps.
inline constexpr int kMaxStepsPerAdvance = 8;

/// PORT CHOICE (T-62): the longest frame delta (ms) the scene lets any frame-rate-driven movement
/// consume. FixedClock already drops everything past kMaxStepsPerAdvance fixed steps, so the sim
/// can never run more than this much game time per frame; the scene's own player motion,
/// regeneration and timers use the SAME bound, so a hitch (debugger pause, window drag, a 2 s
/// stall) slows the game down instead of teleporting the player: every hero walks 6.5 u/s = 208 px/s,
/// so a clamped frame is at most 33.3 px (about one cell), and MoveBody sub-steps even that.
/// The run clock (RunController::RecordTime) still takes the unclamped wall-clock delta.
inline constexpr float kMaxFrameDtMs = kFixedStepMs * static_cast<float>(kMaxStepsPerAdvance);

/// Clamp a frame delta to [0, kMaxFrameDtMs]; NaN / negative / infinite -> 0 or the bound.
inline float ClampFrameDtMs(float dtMs) {
    if (!(dtMs > 0.0F)) { // NaN, 0 and negatives
        return 0.0F;
    }
    return dtMs < kMaxFrameDtMs ? dtMs : kMaxFrameDtMs; // +inf -> bound
}

/// The ms and seconds step constants must stay in lockstep; enforce it at compile
/// time so a future edit to one cannot silently desync the other.
static_assert(kFixedStepMs == kFixedStepSeconds * 1000.0F,
              "kFixedStepMs must equal kFixedStepSeconds * 1000");

/// P3.0 (closes T-25): Unity data units -> screen-world px. Every speed / distance read
/// from the data tables is in Unity UNITS (units/s, units): the Rigidbody2D.velocity
/// writes of RGEController / RGBullet / RGCoin and the RGCoin.range field are all in
/// world units. One unit is 16 source px (every atlas is PPU 16, see
/// Game::Scale::kSourcePxPerUnit) drawn at Scale::kSpriteScale 2 = 32 world px = one
/// map cell (Scale::kCellPx). This is the SINGLE conversion for data speeds and
/// distances (bullet_speed, enemy/hero speed, RGCoin speed/range, GetForce
/// impulses); it is deliberately derived from Scale::kCellPx so it can never drift from
/// the room geometry, the sprites/colliders or Sim::kWorldUnitPx. The shell
/// (GameScene) and the sim's WeaponController share it. Replaces the legacy,
/// unsourced 15 px per unit (ortho 7.5 x 2 guess).
inline constexpr float kDataSpeedToPxPerSec = Game::Scale::kCellPx;
static_assert(kDataSpeedToPxPerSec == 32.0F, "1 Unity unit == 32 world px (PPU 16 at 2x)");

/// FALLBACK collision radii (px), used ONLY when the prefab collider data is missing
/// (a default-constructed def, an entity whose prefab has no collider, an enemy
/// bullet with no resolvable bullet prefab). The live values come from data:
/// Sim::BodyGeom (hero/enemy/boss body circle + hurtbox) and BulletState::radiusPx
/// (the bullet prefab collider) -- see sim/BodyGeom.hpp and tools/build_gamedata.py.
///
/// PORT CHOICE (T-52): ENEMY bullets never carry a prefab radius (the enemy fire path sets
/// FireIntent::radiusPx = 0), so they always use this 4 px fallback instead of their
/// bullet prefab's own collider; only player-weapon bullets (BrainFactory ->
/// EquivalentRadiusPx) use the data radius.
inline constexpr float kBulletRadius = 4.0F;
inline constexpr float kPlayerBodyRadius = 16.0F;
inline constexpr float kEnemyBodyRadius = 16.0F;
inline constexpr float kBossBodyRadius = 24.0F;

/// PORT CHOICE (T-57, swept bullet collision): the player's own displacement between two consecutive
/// fixed steps (WorldInputs::playerPos is supplied by the shell, not integrated by the sim)
/// is folded into a bullet-vs-player sweep only when it is at most this long. A larger jump
/// (dash, door teleport, respawn) is NOT a continuous motion, so the player is then treated
/// as stationary at its new position for that step. 64 px = 2 cells = ~16x the walk speed
/// per step; the value only needs to separate walking from teleporting. The very first step
/// (no previous position yet) also treats the player as stationary. The player moves linearly
/// from the previous step's position to this step's on the same [0,1] step scale as the bullet.
inline constexpr float kMaxPlayerSweepPx = 64.0F;

/// Slice enemy HP. EnemyDef carries no hp field and EnemyController leaves stats.hp=0,
/// so the Simulation seeds it; mirrors the old Enemy entity's CombatStats{3,3,...}.
inline constexpr int kSliceEnemyHp = 3;

/// Repel input scale fed to Combat::AttackerInput: data repel (weapons.json `repel`, 1..~9)
/// -> GetForce power in Unity units/s. PORT CHOICE (T-53, P3.0): identity (1). The previous 30 was
/// documented as a "unit-reconciliation tunable" (PORT_PHASE3_REPORT) from the px/s era: it
/// pushed every hit to the 28 cap, applied in px/s. The units->px reconciliation now lives in
/// ONE place (EnemyController::ComputeVelocity multiplies the knockback by
/// kDataSpeedToPxPerSec), so the repel->power map is left at the data value; the original's
/// real map (bullet repel -> RGEController.GetForce power) is not recovered. The FAITHFUL
/// caps (28 enemy / 30 player, Combat::kEnemyRepelCap) are in the same units/s.
inline constexpr float kRepelScale = 1.0F;

/// Salt mixed into runSeed for the hit-resolution RNG stream, so the crit-roll stream
/// is distinct from every brain stream (which are seeded from runSeed + offsets).
inline constexpr int kHitRngSalt = 0x5170;

/// PORT CHOICE (T-36): C04's melee sword-cut (AtkCut, RGSword spawn) has no
/// recovered reach/hitbox value -- the RGSword prefab's collider dimensions are a
/// truncated get_transform/Instantiate tail-call, out of scope for this port. The
/// sim needs SOME forward hit-test to land the cut, so it uses a small radius+cone
/// query rather than reusing the bullet-circle path (a melee cut is not a bullet).
/// Chosen: reach = 48px (~3x kEnemyBodyRadius, a generous one-body-length "sword
/// swing" past the player's own 16px body) and a 120-degree forward arc (60 degrees
/// either side of the aim direction) -- wide enough that a roughly-aimed cut lands,
/// narrow enough it is not an omnidirectional nuke. Not FAITHFUL; flagged here so a
/// future RGSword-collider recovery can replace it without hunting through
/// Simulation.cpp.
inline constexpr float kMeleeCutReachPx = 48.0F;
inline constexpr float kMeleeCutHalfAngleDeg = 60.0F;

/// PORT CHOICE (T-40): pixels per Unity world unit. The P3.3.c skill bodies are
/// recovered from ARM disassembly in WORLD UNITS (Physics2D radii, Rigidbody2D.velocity
/// units/s: thunder search 14u, chain 10u, bat speed 6u/s, holy light radius 4u).
/// One unit == one map cell == GameScene::kCellPx (32 px) -- the same scale the room
/// geometry already uses. Since P3.0 (T-25 closed) the data-speed factor
/// kDataSpeedToPxPerSec is the same 32; the two names are kept for readability
/// (geometry/skill positions vs. speeds) and both derive from Scale::kCellPx.
inline constexpr float kWorldUnitPx = Game::Scale::kCellPx;
static_assert(kWorldUnitPx == 32.0F, "one unit == one map cell == 32 px");

} // namespace Game::Sim

#endif /* GAME_SIM_SIMCONFIG_HPP */
