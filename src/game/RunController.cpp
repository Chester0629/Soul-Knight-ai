#include "game/RunController.hpp"

#include <memory>

#include "scenes/GameScene.hpp"
#include "scenes/HeroSelectScene.hpp"
#include "scenes/SettlementScene.hpp"
#include "scenes/TitleScene.hpp"

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

void RunController::ShowTitle() {
    auto title = std::make_shared<TitleScene>(this);
    if (m_Scenes.Empty()) {
        m_Scenes.Push(title);
    } else {
        m_Scenes.Replace(title);
    }
}

void RunController::GoToCharacterSelect() {
    auto sel = std::make_shared<HeroSelectScene>(this);
    if (m_Scenes.Empty()) {
        m_Scenes.Push(sel);
    } else {
        m_Scenes.Replace(sel);
    }
}

void RunController::BeginRun(const std::string &charId) {
    m_State.selectedCharId = charId;
    ResetRunState(); // floor 0 / template / Playing (keeps runSeed + selectedCharId).
    auto floor0 = BuildFloorScene();
    if (m_Scenes.Empty()) {
        m_Scenes.Push(floor0);
    } else {
        m_Scenes.Replace(floor0);
    }
}

void RunController::OnPlayerDied() {
    // Death -> Defeat settlement (shown on the floor we died on; NOT advanced).
    // Replace (not Push) so the dead GameScene is exited, not leaked under the
    // results screen. Deferred while the dying scene's Update runs (same as a clear).
    const int deathFloor = m_State.floorIndex;
    m_State.phase = RunState::Phase::Ended;
    m_Scenes.Replace(std::make_shared<SettlementScene>(
        this, SettlementScene::Outcome::Defeat, deathFloor));
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
    // Chapter routing: was the floor we JUST cleared the chapter's boss floor?
    // Capture BEFORE AdvanceFloor (which increments floorIndex). If so the chapter
    // is done -> Victory settlement; otherwise advance to the next (mob) floor.
    const int clearedFloor = m_State.floorIndex;
    const bool wasBossFloor = IsBossFloor(clearedFloor);
    AdvanceFloor(snapshot);
    if (wasBossFloor) {
        m_State.phase = RunState::Phase::Ended;
        m_Scenes.Replace(std::make_shared<SettlementScene>(
            this, SettlementScene::Outcome::Victory, clearedFloor));
    } else {
        m_Scenes.Replace(BuildFloorScene());
    }
}

} // namespace Game
