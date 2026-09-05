#ifndef GAME_END_SCENE_HPP
#define GAME_END_SCENE_HPP

#include <memory>
#include <string>
#include <vector>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"

#include "data/GameData.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in EndScene.cpp.

/**
 * @class EndScene
 * @brief B4 results screen -- the port of the original's Statements scene
 *        (`_RGScene/Statements.unity`, driven by UIStatement): title
 *        (i2 I_gameover / I_victory), the level label from UIStatement.level_list
 *        ("1-1".."3-6"), the hero's portrait in the ui2_0 frame, and the
 *        counters the original shows -- clock (ui_10, "h:mm:ss"), kills (ui_11),
 *        coins (ui_5) and this run's gems (ui_52, "+N") -- read from RunState's
 *        RunStats. Every sprite is the original's (resolved through the scene's
 *        guids); the numbers are the run's REAL counters (coins/gems are 0 until
 *        the economy is ported -- shown as 0, never faked).
 *
 * Input, following UIStatement.Update (Submit / Cancel / Escape all fire
 * OnBtnClick, which leaves for the hub): **Enter / Space / click / Esc -> main
 * menu** (the port's hub stand-in). Port conveniences: **R** restarts a fresh run
 * as the same hero, **Q** quits.
 *
 * Holds a `RunController*` to drive the actions; `ShowMenu` / `StartRun` both
 * **Replace** this scene (the stack is non-empty), never `Push`. GameObjects are
 * built lazily on first `Render` (`Util::Text` / `Util::Image` need a live GL
 * context).
 */
class EndScene : public Core::Scene {
public:
    explicit EndScene(RunController *run) : m_Run(run) {}

    void OnEnter() override;
    void OnExit() override;
    void Update(float dtMs) override;
    void Render() override;

private:
    void Build();

    RunController *m_Run;
    GameData m_Data;
    Util::Renderer m_Renderer;
    std::vector<std::shared_ptr<Util::GameObject>> m_Objects;
    bool m_Built = false;

    // --- Headless test hook (env-gated, NO-OP by default) ---
    /// Frames since OnEnter, for SK_ENDSCENE.
    int m_Frame = 0;
    /// SK_ENDSCENE=restart|quit|menu auto-picks the action after a few frames so the
    /// death->EndScene->restart/quit/menu path can be driven without input. -1 =
    /// wait for real input (normal play).
    int m_AutoActionFrame = -1;
    enum class AutoAction { None, Restart, Quit, Menu } m_AutoAction = AutoAction::None;
};
} // namespace Game

#endif /* GAME_END_SCENE_HPP */
