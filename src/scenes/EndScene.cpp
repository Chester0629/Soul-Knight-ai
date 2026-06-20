#include "scenes/EndScene.hpp"

#include <cstdlib>
#include <string>

#include <glm/glm.hpp>

#include "Core/Context.hpp"

#include "Util/Color.hpp"
#include "Util/Input.hpp"
#include "Util/Keycode.hpp"
#include "Util/Logger.hpp"
#include "Util/Text.hpp"
#include "Util/TransformUtils.hpp"

#include "game/RunController.hpp"

namespace Game {
namespace {
// Screen-space text GameObject: centre-origin coords, +y up (see TransformUtils:
// translation {0,0} is the screen centre). The text quad is centred on @p pos.
std::shared_ptr<Util::GameObject> MakeText(const std::string &font, int size,
                                           const std::string &text, glm::vec2 pos) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::make_shared<Util::Text>(
        font, size, text, Util::Color{255, 255, 255, 255}));
    obj->m_Transform.translation = pos;
    obj->SetZIndex(50.0F);
    return obj;
}
} // namespace

void EndScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string font = root + "/fonts/pixel_bold.ttf";
    m_Texts.push_back(MakeText(font, 36, "YOU DIED", {0.0F, 40.0F}));
    m_Texts.push_back(MakeText(font, 18, "R: new run     Esc: quit", {0.0F, -20.0F}));
    for (const auto &t : m_Texts) {
        m_Renderer.AddChild(t);
    }
    m_Built = true;
}

void EndScene::OnEnter() {
    LOG_INFO("EndScene: you died -- R: new run, Esc: quit");
    // SK_ENDSCENE=restart|quit test hook (env, NO-OP if unset): auto-pick the action
    // after a few frames, so the death->EndScene->restart/quit path runs headlessly.
    if (const char *act = std::getenv("SK_ENDSCENE")) {
        const std::string a = act;
        if (a == "restart") {
            m_AutoAction = AutoAction::Restart;
            m_AutoActionFrame = 30;
        } else if (a == "quit") {
            m_AutoAction = AutoAction::Quit;
            m_AutoActionFrame = 30;
        }
    }
}

void EndScene::OnExit() {
    LOG_INFO("EndScene: OnExit (left the run-end screen)");
}

void EndScene::Update(float /*dtMs*/) {
    ++m_Frame;
    const bool autoFire = (m_AutoActionFrame >= 0 && m_Frame >= m_AutoActionFrame);

    const bool restart = Util::Input::IsKeyDown(Util::Keycode::R) ||
                         (autoFire && m_AutoAction == AutoAction::Restart);
    const bool quit = Util::Input::IsKeyUp(Util::Keycode::ESCAPE) ||
                      Util::Input::IsKeyDown(Util::Keycode::Q) || Util::Input::IfExit() ||
                      (autoFire && m_AutoAction == AutoAction::Quit);

    if (restart && m_Run != nullptr) {
        LOG_INFO("EndScene: restart -> new run (floor 0, template)");
        // StartRun resets the run state (floor 0 / carried cleared / Playing) and,
        // because the stack is non-empty, REPLACES this EndScene with the fresh
        // floor-0 GameScene (deferred while we are inside the manager's Update).
        m_Run->StartRun();
        return; // do not also process quit this frame.
    }
    if (quit) {
        LOG_INFO("EndScene: quit -> exit");
        Core::Context::GetInstance()->SetExit(true);
    }
}

void EndScene::Render() {
    if (!m_Built) {
        Build(); // lazy: Util::Text needs a live GL context (run from the render loop).
    }
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen space (identity view)
    m_Renderer.Update();
}

} // namespace Game
