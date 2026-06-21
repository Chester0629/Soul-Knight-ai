#ifndef GAME_TITLE_SCENE_HPP
#define GAME_TITLE_SCENE_HPP

#include <memory>
#include <vector>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in the .cpp.

/**
 * @class TitleScene
 * @brief The login / title screen (the "Soul Knight" splash from the reference
 *        video): logo + a hero figure + a "press start" prompt. Enter/Space (or
 *        the SK_TITLE test hook) advances to the HeroSelectScene.
 *
 * Greenfield `Core::Scene`, modeled on EndScene/SettlementScene: render-only
 * (screen-space), lazy-built on first Render (Util::Text/Image need a live GL
 * context), holds a `RunController*` to drive the transition.
 */
class TitleScene : public Core::Scene {
public:
    explicit TitleScene(RunController *run) : m_Run(run) {}

    void OnEnter() override;
    void OnExit() override;
    void Update(float dtMs) override;
    void Render() override;

private:
    void Build();

    RunController *m_Run;
    Util::Renderer m_Renderer;
    std::vector<std::shared_ptr<Util::GameObject>> m_Objects;
    bool m_Built = false;

    // --- Headless test hook (env-gated, NO-OP by default), mirrors SK_ENDSCENE. ---
    int m_Frame = 0;
    int m_AutoStartFrame = -1; ///< SK_TITLE=start: auto-advance after N frames.
};
} // namespace Game

#endif /* GAME_TITLE_SCENE_HPP */
