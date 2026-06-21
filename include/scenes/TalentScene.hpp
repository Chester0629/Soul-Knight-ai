#ifndef GAME_TALENT_SCENE_HPP
#define GAME_TALENT_SCENE_HPP

#include <memory>
#include <vector>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in the .cpp.

/**
 * @class TalentScene
 * @brief The chapter-start "select talent" screen from the reference video: three
 *        stat perks (HP +4 / Armor +1 / Energy +100); pick one and the chapter
 *        starts. A/D or 1/2/3 to choose, Enter to confirm.
 *
 * Greenfield `Core::Scene`, modeled on the other front-end scenes: render-only,
 * lazy-built, drives RunController::ChooseTalent (which records the bonus and
 * builds floor 0). The bonus is applied once to the floor-0 player in GameScene.
 */
class TalentScene : public Core::Scene {
public:
    explicit TalentScene(RunController *run) : m_Run(run) {}

    void OnEnter() override;
    void OnExit() override;
    void Update(float dtMs) override;
    void Render() override;

private:
    void Build();
    void ApplyHighlight();
    void Confirm();

    RunController *m_Run;
    Util::Renderer m_Renderer;
    bool m_Built = false;

    int m_Sel = 0;
    int m_LastApplied = -1;
    std::vector<std::shared_ptr<Util::GameObject>> m_Cards; ///< one per option (label).

    // --- Headless test hook (env-gated, NO-OP by default). ---
    int m_Frame = 0;
    int m_AutoConfirmFrame = -1; ///< SK_TALENT=N: pick option N + confirm.
};
} // namespace Game

#endif /* GAME_TALENT_SCENE_HPP */
