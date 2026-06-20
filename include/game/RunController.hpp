#ifndef GAME_RUN_CONTROLLER_HPP
#define GAME_RUN_CONTROLLER_HPP

#include <memory>

#include "Core/Scene.hpp"
#include "Core/SceneManager.hpp"

#include "game/RunState.hpp"

namespace Game {

/**
 * @class RunController
 * @brief Run-level orchestrator (the `RGGameProcess` analog). Owns the @ref
 *        RunState AND the `Core::SceneManager` that holds the per-floor
 *        `GameScene`s (RUN_LOOP_PLAN D1).
 *
 * @par Lifetime / ownership (D1)
 * `main` owns ONLY the RunController; the controller owns the SceneManager,
 * declared **last** so it is torn down **first** -- while the controller object
 * still exists. (Next step gives each scene a `RunController*` back-pointer to
 * signal floor-clear/death; the referent must outlive every scene pointing at
 * it. Declaring the stack last guarantees that.)
 *
 * @par Phase-1 step-1 scope
 * Wires `StartRun` (cold start / restart) + the **pure** carry/seed logic
 * (@ref OnFloorCleared advances state; @ref CurrentFloorSeed derives the seed).
 * The floor-clear/death SIGNALLING from `GameScene` back into the controller
 * (the `OnFloorCleared -> Replace` transition, `OnPlayerDied -> EndScene`) is the
 * NEXT step and is deliberately NOT wired here -- `OnFloorCleared` currently only
 * advances the pure state, it does not `Replace`.
 */
class RunController {
public:
    RunController() = default;
    explicit RunController(int runSeed) { m_State.runSeed = runSeed; }

    /// Start (or restart) a run: reset the per-run progress (floor 0 / template /
    /// Playing) while KEEPING @ref RunState::runSeed, then put floor 0 on the
    /// stack. Uses `Empty() ? Push : Replace` so the SAME entry handles cold start
    /// and a later restart (from a future EndScene) without leaking a scene.
    void StartRun();

    /// Reset the per-run progress to a FRESH run -- floor 0, continuation snapshot
    /// CLEARED, phase Playing -- while KEEPING `runSeed`. PURE state mutation,
    /// unit-testable; both @ref StartRun and the EndScene restart go through it so a
    /// restart is a brand-new run from the template, never a continuation of the
    /// dead floor (RUN_LOOP_PLAN step 2b constraint).
    void ResetRunState();

    /// Player died: mark the run `Ended` and `Replace` the active scene with a
    /// minimal `EndScene`. Requested from the dying `GameScene`'s `Update`, so the
    /// `Replace` is DEFERRED (same transition timing as a floor clear). Death is no
    /// longer an app quit.
    void OnPlayerDied();

    /// Advance the run STATE to the next floor with this carried snapshot: PURE,
    /// engine-free (`carried = snapshot; ++floorIndex`). Unit-testable in isolation.
    /// @ref OnFloorCleared calls this and then performs the scene transition.
    void AdvanceFloor(const PlayerContinuation &snapshot);

    /// Floor-clear signal from the active `GameScene` (called from its `Update`
    /// while the scene -- and its `m_Player` -- are STILL ALIVE). Captures the
    /// snapshot via @ref AdvanceFloor, then requests a `SceneManager::Replace` into
    /// the next floor's `GameScene`. The `Replace` is DEFERRED (we are inside the
    /// manager's `Update`), so the signalling scene outlives this snapshot read --
    /// the capture-before-destroy contract (step 2a).
    void OnFloorCleared(const PlayerContinuation &snapshot);

    /// Per-floor base seed for the CURRENT floor = `PerFloorSeed(runSeed, floorIndex)`.
    int CurrentFloorSeed() const {
        return PerFloorSeed(m_State.runSeed, m_State.floorIndex);
    }

    /// Forward the frame to the owned scene stack (the main loop calls these).
    void Update(float dtMs) { m_Scenes.Update(dtMs); }
    void Render() { m_Scenes.Render(); }

    const RunState &State() const { return m_State; }

private:
    /// Build the `GameScene` for the current floor (seed + index + carried).
    std::shared_ptr<Core::Scene> BuildFloorScene();

    RunState m_State;
    /// Declared LAST: destroyed FIRST, so the scenes are torn down while `*this`
    /// is still alive (D1 lifetime invariant).
    Core::SceneManager m_Scenes;
};

} // namespace Game

#endif /* GAME_RUN_CONTROLLER_HPP */
