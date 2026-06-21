#ifndef GAME_HERO_SELECT_SCENE_HPP
#define GAME_HERO_SELECT_SCENE_HPP

#include <memory>
#include <string>
#include <vector>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"
#include "Util/Text.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in the .cpp.

/**
 * @class HeroSelectScene
 * @brief The simplified character-select menu (the user chose a menu over the
 *        walkable HQ hub). A/D or Left/Right switches hero, Enter confirms ->
 *        RunController::BeginRun(charId); Esc goes back to the title.
 *
 * Greenfield `Core::Scene`, modeled on the other front-end scenes: render-only,
 * lazy-built. Only c01/c02 have faithful skills today (c03+ are gate+cooldown
 * stubs), so the roster starts small and is trivial to extend.
 */
class HeroSelectScene : public Core::Scene {
public:
    explicit HeroSelectScene(RunController *run) : m_Run(run) {}

    void OnEnter() override;
    void OnExit() override;
    void Update(float dtMs) override;
    void Render() override;

private:
    void Build();
    void ApplyHighlight(); ///< reflect m_Sel: scale the chosen hero up + set the name.

    RunController *m_Run;
    Util::Renderer m_Renderer;
    bool m_Built = false;

    int m_Sel = 0;        ///< selected roster index.
    int m_LastApplied = -1; ///< last m_Sel reflected by ApplyHighlight (avoid re-render).
    std::vector<std::string> m_CharIds;  ///< "c01", "c02", ...
    std::vector<std::string> m_Names;    ///< display labels.
    std::vector<std::shared_ptr<Util::GameObject>> m_CharObjs; ///< per-hero idle sprite.
    std::shared_ptr<Util::Text> m_NameText; ///< the big centred hero-name label.

    // --- Headless test hook (env-gated, NO-OP by default). ---
    int m_Frame = 0;
    int m_AutoConfirmFrame = -1; ///< SK_SELECT=cNN: select that hero + confirm after N frames.
};
} // namespace Game

#endif /* GAME_HERO_SELECT_SCENE_HPP */
