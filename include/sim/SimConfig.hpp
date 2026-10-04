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

/// The ms and seconds step constants must stay in lockstep; enforce it at compile
/// time so a future edit to one cannot silently desync the other.
static_assert(kFixedStepMs == kFixedStepSeconds * 1000.0F,
              "kFixedStepMs must equal kFixedStepSeconds * 1000");

/// T-25 (unit audit pending, P3.0): 15 px per data unit here vs. 32 px per map cell
/// in GameScene::kCellPx. The decompile (T-26 truncation) does not show how RGBullet
/// applies `speed`; the source atlas PPU is 16 (2x render = 32 px/unit).
/// Data bulletSpeed (raw units from the JSON tables) -> pixels/second. The shell
/// (GameScene) and the sim's WeaponController must agree on this factor, so it
/// lives here rather than as an anonymous constant. Mirrors GameScene's historical
/// kBulletSpeedScale; promote that to use this when the weapon path is wired.
inline constexpr float kDataSpeedToPxPerSec = 15.0F;

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

/// Slice enemy HP. EnemyDef carries no hp field and EnemyController leaves stats.hp=0,
/// so the Simulation seeds it; mirrors the old Enemy entity's CombatStats{3,3,...}.
inline constexpr int kSliceEnemyHp = 3;

/// Repel input scale fed to Combat::AttackerInput (mirrors GameScene's kRepelScale=30).
inline constexpr float kRepelScale = 30.0F;

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
/// geometry already uses; T-25 (the RGBullet `speed` data-unit audit) is a separate,
/// still-pending question and is NOT reused here because Rigidbody2D.velocity is a
/// direct units/s value, not a bullet-data speed.
inline constexpr float kWorldUnitPx = Game::Scale::kCellPx;
static_assert(kWorldUnitPx == 32.0F, "one unit == one map cell == 32 px");

} // namespace Game::Sim

#endif /* GAME_SIM_SIMCONFIG_HPP */
