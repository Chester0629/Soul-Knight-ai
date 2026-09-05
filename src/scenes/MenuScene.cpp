#include "scenes/MenuScene.hpp"

#include <cstdlib>
#include <string>

#include <glm/glm.hpp>

#include "Core/Context.hpp"

#include "Util/Color.hpp"
#include "Util/Image.hpp"
#include "Util/Input.hpp"
#include "Util/Keycode.hpp"
#include "Util/Logger.hpp"
#include "Util/Text.hpp"
#include "Util/TransformUtils.hpp"

#include "game/RunController.hpp"

namespace Game {
namespace {
// Screen-space text GameObject: centre-origin coords, +y up (TransformUtils:
// translation {0,0} is the screen centre). Util::Text("") crashes (see Hud.cpp),
// so every label is non-empty.
std::shared_ptr<Util::GameObject> MakeText(const std::string &font, int size,
                                           const std::string &text, glm::vec2 pos) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::make_shared<Util::Text>(
        font, size, text, Util::Color{255, 255, 255, 255}));
    obj->m_Transform.translation = pos;
    obj->SetZIndex(50.0F);
    return obj;
}
/// title_en0.png is 286x63 pixel art; 2.5x keeps it crisp at ~715x158 on 1280x720.
constexpr float kTitleScale = 2.5F;
} // namespace

void MenuScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string font = root + "/fonts/pixel_bold.ttf";

    // FAITHFUL: the original's title art.
    auto title = std::make_shared<Util::GameObject>();
    title->SetDrawable(std::make_shared<Util::Image>(root + "/sprites/title_en0.png"));
    title->m_Transform.translation = {0.0F, 110.0F};
    title->m_Transform.scale = {kTitleScale, kTitleScale};
    title->SetZIndex(50.0F);
    m_Objects.push_back(title);

    m_Objects.push_back(MakeText(font, 22, "Enter / Space / click:  START", {0.0F, -60.0F}));
    m_Objects.push_back(MakeText(font, 16, "Esc: quit", {0.0F, -110.0F}));
    m_Objects.push_back(MakeText(font, 12, "Soul-Knight-ai  --  faithful C++ port (PTSD)",
                                 {0.0F, -300.0F}));
    for (const auto &o : m_Objects) {
        m_Renderer.AddChild(o);
    }
    m_Built = true;
}

void MenuScene::OnEnter() {
    LOG_INFO("MenuScene: main menu -- Enter/Space/click: start, Esc: quit");
    if (const char *a = std::getenv("SK_MENU"); a != nullptr && std::string(a) == "start") {
        m_AutoStartFrame = 30;
    }
}

void MenuScene::OnExit() {
    LOG_INFO("MenuScene: OnExit");
}

void MenuScene::Update(float /*dtMs*/) {
    ++m_Frame;
    const bool autoStart = (m_AutoStartFrame >= 0 && m_Frame >= m_AutoStartFrame);
    const bool start = Util::Input::IsKeyDown(Util::Keycode::RETURN) ||
                       Util::Input::IsKeyDown(Util::Keycode::SPACE) ||
                       Util::Input::IsKeyDown(Util::Keycode::MOUSE_LB) || autoStart;
    const bool quit = Util::Input::IsKeyUp(Util::Keycode::ESCAPE) || Util::Input::IfExit();

    if (start && m_Run != nullptr) {
        LOG_INFO("MenuScene: start -> hero select");
        m_Run->ShowCharSelect(); // deferred Replace (we are inside the manager's Update)
        return;
    }
    if (quit) {
        LOG_INFO("MenuScene: quit -> exit");
        Core::Context::GetInstance()->SetExit(true);
    }
}

void MenuScene::Render() {
    if (!m_Built) {
        Build(); // lazy: Util::Text / Util::Image need a live GL context.
    }
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen space (identity view)
    m_Renderer.Update();
}

} // namespace Game
