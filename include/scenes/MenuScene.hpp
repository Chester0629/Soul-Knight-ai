#ifndef GAME_MENU_SCENE_HPP
#define GAME_MENU_SCENE_HPP

#include <memory>
#include <optional>
#include <vector>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"

#include "data/GameData.hpp"
#include "game/AudioPlayer.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in MenuScene.cpp.

/**
 * @class MenuScene
 * @brief B6 main menu: the original's title art (Resources/sprites/title_en0.png)
 *        plus start / quit prompts. Enter, Space or a click -> hero select (B5);
 *        Esc -> quit. Screen-space, owns no run state; built lazily on the first
 *        Render (Util::Text / Util::Image need a live GL context).
 *
 * Headless hook (env, NO-OP by default): SK_MENU=start auto-advances to the hero
 * select after a few frames, so the menu -> select -> run path can be driven and
 * screenshotted without input.
 */
class MenuScene : public Core::Scene {
public:
    explicit MenuScene(RunController *run) : m_Run(run) {}

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

    /// AUDIO: menu BGM + start-button click (audio.json bgm.menu/menu_button --
    /// UITitle.bgm_clip/.btn_clip). Loaded/constructed in OnEnter.
    GameData m_Data;
    std::optional<AudioPlayer> m_Audio;

    int m_Frame = 0;
    int m_AutoStartFrame = -1; ///< SK_MENU=start: frame to auto-advance; -1 = wait for input.
};

} // namespace Game

#endif /* GAME_MENU_SCENE_HPP */
