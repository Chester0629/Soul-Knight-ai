#ifndef GAME_CHAR_SELECT_SCENE_HPP
#define GAME_CHAR_SELECT_SCENE_HPP

#include <memory>
#include <string>
#include <vector>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"
#include "Util/Text.hpp"

#include "data/GameData.hpp"
#include "game/CharSelectModel.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in the .cpp.

/**
 * @class CharSelectScene
 * @brief B5 hero select -- a select-screen stand-in for the original's walkable
 *        HeroRoom (the hub is the later hub work (6)). Shows all 13 heroes as a row of
 *        their REAL idle sprites (sprite manifest), the selected one animated
 *        large with its REAL name (i2 localization) and base stats
 *        (characters.json, per-character re-key). A/D or arrows cycle (wrapping),
 *        Enter / Space / click starts the run as that hero, Esc returns to the
 *        menu. State lives in the pure CharSelectModel.
 *
 * Headless hook (env, NO-OP by default): SK_CHAR=cNN pre-selects that hero and
 * auto-starts after a few frames.
 */
class CharSelectScene : public Core::Scene {
public:
    explicit CharSelectScene(RunController *run) : m_Run(run) {}

    void OnEnter() override;
    void OnExit() override;
    void Update(float dtMs) override;
    void Render() override;

private:
    void Build();
    void Refresh(); ///< re-point the preview / marker / texts at the current hero

    RunController *m_Run;
    GameData m_Data;
    CharSelectModel m_Model{std::vector<std::string>{}};
    Util::Renderer m_Renderer;
    std::vector<std::shared_ptr<Util::GameObject>> m_Thumbs;
    std::shared_ptr<Util::GameObject> m_Preview;
    std::shared_ptr<Util::GameObject> m_Marker;
    std::shared_ptr<Util::Text> m_NameText;
    std::shared_ptr<Util::Text> m_StatsText;
    std::shared_ptr<Util::Text> m_SkillText;
    std::shared_ptr<Util::Text> m_IndexText;
    bool m_Built = false;

    int m_Frame = 0;
    int m_AutoStartFrame = -1; ///< SK_CHAR: frame to auto-start; -1 = wait for input.
};

} // namespace Game

#endif /* GAME_CHAR_SELECT_SCENE_HPP */
