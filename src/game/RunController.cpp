#include "game/RunController.hpp"

#include <memory>

#include "scenes/EndScene.hpp"
#include "scenes/GameScene.hpp"

namespace Game {

std::shared_ptr<Core::Scene> RunController::BuildFloorScene() {
    return std::make_shared<GameScene>(CurrentFloorSeed(), m_State.floorIndex,
                                       m_State.carried, this);
}

void RunController::ResetRunState() {
    // Fresh run: floor 0, NO continuation, Playing. Keep the base seed (so a restart
    // replays the same run unless runSeed is changed externally).
    m_State.floorIndex = 0;
    m_State.carried.reset();
    m_State.phase = RunState::Phase::Playing;
}

void RunController::StartRun() {
    ResetRunState();

    auto floor0 = BuildFloorScene();
    if (m_Scenes.Empty()) {
        m_Scenes.Push(floor0);
    } else {
        // Restart path (from the EndScene): Replace so the previous top (the
        // EndScene) is exited, never stacked-over-and-leaked.
        m_Scenes.Replace(floor0);
    }
}

void RunController::OnPlayerDied() {
    m_State.phase = RunState::Phase::Ended;
    // Replace (not Push) so the dead GameScene is exited, not leaked under the
    // EndScene. Deferred while the dying scene's Update runs (same as a clear).
    m_Scenes.Replace(std::make_shared<EndScene>(this));
}

void RunController::AdvanceFloor(const PlayerContinuation &snapshot) {
    // Pure carry: remember the snapshot + advance the floor counter. No engine.
    m_State.carried = snapshot;
    m_State.floorIndex += 1;
}

void RunController::OnFloorCleared(const PlayerContinuation &snapshot) {
    // Capture-before-destroy: AdvanceFloor copies the snapshot into RunState NOW
    // (the caller passed it while its scene was alive). The Replace is then DEFERRED
    // by the SceneManager (we are inside its Update via the signalling GameScene), so
    // the old scene + its m_Player are not torn down until after this returns -- the
    // snapshot is safely in RunState by then. The next GameScene loads it on OnEnter.
    AdvanceFloor(snapshot);
    m_Scenes.Replace(BuildFloorScene());
}

} // namespace Game
