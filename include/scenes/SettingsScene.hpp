#ifndef GAME_SETTINGS_SCENE_HPP
#define GAME_SETTINGS_SCENE_HPP

#include <array>
#include <memory>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in the .cpp.

/**
 * @class SettingsScene
 * @brief The settings screen from the reference video: Master / BGM / SFX volume
 *        sliders. W/S select a row, A/D adjust, Esc returns to the title.
 *
 * Greenfield `Core::Scene`, render-only, lazy-built. Volume values persist for the
 * session (static) and are kept here as the single store; wiring them into live
 * audio mixing is a later pass (the screen + adjustment are faithful now).
 */
class SettingsScene : public Core::Scene {
public:
    explicit SettingsScene(RunController *run) : m_Run(run) {}

    void OnEnter() override;
    void OnExit() override;
    void Update(float dtMs) override;
    void Render() override;

    /// Session-persistent volumes (0..100): master, bgm, sfx.
    static std::array<int, 3> s_Volumes;

private:
    void Build();
    void Refresh(); ///< reflect m_Sel highlight + the 3 slider fills.

    RunController *m_Run;
    Util::Renderer m_Renderer;
    bool m_Built = false;
    int m_Sel = 0;
    int m_LastSel = -1;
    std::array<int, 3> m_LastVals{-1, -1, -1};
    std::shared_ptr<Util::GameObject> m_Marker;
    std::array<std::shared_ptr<Util::GameObject>, 3> m_Fills;
    int m_Frame = 0;
    int m_AutoBackFrame = -1; ///< SK_SETTINGS=back: auto-return after N frames.
};
} // namespace Game

#endif /* GAME_SETTINGS_SCENE_HPP */
