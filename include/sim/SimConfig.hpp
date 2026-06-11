#ifndef GAME_SIM_SIMCONFIG_HPP
#define GAME_SIM_SIMCONFIG_HPP

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

/// Data bulletSpeed (raw units from the JSON tables) -> pixels/second. The shell
/// (GameScene) and the sim's WeaponController must agree on this factor, so it
/// lives here rather than as an anonymous constant. Mirrors GameScene's historical
/// kBulletSpeedScale; promote that to use this when the weapon path is wired.
inline constexpr float kDataSpeedToPxPerSec = 15.0F;

/// Slice collision radii (px). The sim is geometry-light: bodies/bullets are circles.
/// These mirror GameScene's kPlayerRadius(16)/kBossBodyRadius(24) placeholders.
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

} // namespace Game::Sim

#endif /* GAME_SIM_SIMCONFIG_HPP */
