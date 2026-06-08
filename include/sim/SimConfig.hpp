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

} // namespace Game::Sim

#endif /* GAME_SIM_SIMCONFIG_HPP */
