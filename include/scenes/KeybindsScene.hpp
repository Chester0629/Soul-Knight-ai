#ifndef GAME_KEYBINDS_SCENE_HPP
#define GAME_KEYBINDS_SCENE_HPP

#include <memory>
#include <vector>

#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"

namespace Game {
class RunController; // forward: back-pointer only; full type included in the .cpp.

/**
 * @class KeybindsScene
 * @brief The controls / keybindings help page from the reference video. Static
 *        list of bindings; Esc returns to the title.
 */
class KeybindsScene : public Core::Scene {
public:
    explicit KeybindsScene(RunController *run) : m_Run(run) {}

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
    int m_Frame = 0;
    int m_AutoBackFrame = -1;
};
} // namespace Game

#endif /* GAME_KEYBINDS_SCENE_HPP */
