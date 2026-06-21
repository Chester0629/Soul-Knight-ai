#ifndef GAME_SETTLEMENT_SCENE_HPP
#define GAME_SETTLEMENT_SCENE_HPP

#include <memory>
#include <vector>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in the .cpp.

/**
 * @class SettlementScene
 * @brief Run-end / chapter-end results screen -- the game-over / settlement
 *        screen from the reference video. Supersedes the text-only EndScene: shown on
 *        player death (Defeat) AND on clearing the chapter's boss floor (Victory).
 *
 * Greenfield `Core::Scene`, modeled on EndScene: render-only (screen-space),
 * owns no combat state, holds a `RunController*` to drive restart / quit. Text
 * GameObjects are built lazily on first `Render` (`Util::Text` needs a live GL
 * context). The richer stats panel (time/gold/kills/gems) + floor-progress track +
 * reward chest from the video are a later visual-polish pass; this lands the
 * structure + the correct Victory/Defeat ROUTING first.
 */
class SettlementScene : public Core::Scene {
public:
    enum class Outcome { Victory, Defeat };

    /// @param run     owning controller (restart / quit).
    /// @param outcome Victory (chapter boss floor cleared) or Defeat (player died).
    /// @param displayFloorIndex 0-based floor the run ended on (the CLEARED boss
    ///        floor for Victory, the death floor for Defeat); shown as chapter-floor.
    SettlementScene(RunController *run, Outcome outcome, int displayFloorIndex)
        : m_Run(run), m_Outcome(outcome), m_FloorIndex(displayFloorIndex) {}

    void OnEnter() override;
    void OnExit() override;
    void Update(float dtMs) override;
    void Render() override;

private:
    void Build();

    RunController *m_Run;
    Outcome m_Outcome;
    int m_FloorIndex;
    Util::Renderer m_Renderer;
    std::vector<std::shared_ptr<Util::GameObject>> m_Objects;
    bool m_Built = false;

    // --- Headless test hook (env-gated, NO-OP by default), mirrors SK_ENDSCENE. ---
    int m_Frame = 0;
    int m_AutoActionFrame = -1;
    enum class AutoAction { None, Restart, Quit } m_AutoAction = AutoAction::None;
};
} // namespace Game

#endif /* GAME_SETTLEMENT_SCENE_HPP */
