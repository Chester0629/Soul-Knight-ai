#ifndef GAME_END_SCENE_HPP
#define GAME_END_SCENE_HPP

#include <memory>
#include <vector>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in EndScene.cpp.

/**
 * @class EndScene
 * @brief Minimal run-end screen (Phase-1 step 2b): one line of end text + two
 *        choices -- **R: new run**, **Esc/Q: quit**. Replaces the old "death = quit
 *        the app" behaviour with a visible end state that can restart.
 *
 * Greenfield `Core::Scene`. Render-only (screen-space text), owns no combat state.
 * Holds a `RunController*` to drive the two actions:
 *   - restart -> `run->StartRun()` which **Replaces** this EndScene (the stack is
 *     non-empty) with a FRESH floor-0 `GameScene` -- a brand-new run from the
 *     character template, NOT a continuation of the dead floor. Never `Push` (that
 *     would leave this EndScene leaked at the bottom of the stack).
 *   - quit -> `Context::SetExit(true)`.
 *
 * The polished death / results / reborn UI is a separate, later UI line; this is a
 * deliberate minimal placeholder. Text GameObjects are built lazily on first
 * `Render` (`Util::Text` needs a live GL context).
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
    Util::Renderer m_Renderer;
    std::vector<std::shared_ptr<Util::GameObject>> m_Texts;
    bool m_Built = false;

    // --- Headless test hook (env-gated, NO-OP by default) ---
    /// Frames since OnEnter, for SK_ENDSCENE.
    int m_Frame = 0;
    /// SK_ENDSCENE=restart|quit auto-picks the action after a few frames so the
    /// death->EndScene->restart/quit path can be driven without input. -1 = wait
    /// for real input (normal play).
    int m_AutoActionFrame = -1;
    enum class AutoAction { None, Restart, Quit } m_AutoAction = AutoAction::None;
};
} // namespace Game

#endif /* GAME_END_SCENE_HPP */
