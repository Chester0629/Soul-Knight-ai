#include "scenes/TitleScene.hpp"

#include <cstdlib>
#include <string>

#include <glm/glm.hpp>

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
std::shared_ptr<Util::GameObject> MakeImage(const std::string &path, glm::vec2 pos,
                                            float scale, float z) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::make_shared<Util::Image>(path));
    obj->m_Transform.translation = pos;
    obj->m_Transform.scale = glm::vec2(scale, scale);
    obj->SetZIndex(z);
    return obj;
}
std::shared_ptr<Util::GameObject> MakeText(const std::string &font, int size,
                                           const std::string &text, glm::vec2 pos,
                                           Util::Color color, float z) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::make_shared<Util::Text>(font, size, text, color));
    obj->m_Transform.translation = pos;
    obj->SetZIndex(z);
    return obj;
}
} // namespace

void TitleScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string sprites = root + "/sprites/";
    const std::string font = root + "/fonts/pixel_bold.ttf";

    // Soul Knight logo (biaoti03) up top, a hero figure (c01 idle) centred, a
    // tap-to-start prompt at the bottom -- the video's login layout. (Real dark
    // bg art / green-spotlight figure / gear+version come in the visual pass.)
    m_Objects.push_back(MakeImage(sprites + "biaoti03.png", {0.0F, 210.0F}, 1.2F, 10.0F));
    m_Objects.push_back(MakeImage(sprites + "c01_4.png", {0.0F, -10.0F}, 4.0F, 8.0F));
    m_Objects.push_back(MakeText(font, 24, "PRESS ENTER TO START", {0.0F, -210.0F},
                                 Util::Color{235, 235, 235, 255}, 20.0F));
    m_Objects.push_back(MakeText(font, 16, "S: SETTINGS     K: CONTROLS",
                                 {0.0F, -270.0F}, Util::Color{170, 170, 170, 255}, 20.0F));
    m_Objects.push_back(MakeText(font, 14, "Soul Knight -- front-end flow",
                                 {0.0F, -320.0F}, Util::Color{150, 150, 150, 255}, 20.0F));

    for (const auto &o : m_Objects) {
        m_Renderer.AddChild(o);
    }
    m_Built = true;
}

void TitleScene::OnEnter() {
    LOG_INFO("TitleScene: press Enter/Space to start");
    // SK_TITLE=start|settings|keybinds: auto-dispatch that action (headless).
    if (std::getenv("SK_TITLE") != nullptr) {
        m_AutoStartFrame = 20;
    }
}

void TitleScene::OnExit() { LOG_INFO("TitleScene: OnExit (entering hero select)"); }

void TitleScene::Update(float /*dtMs*/) {
    ++m_Frame;
    if (m_AutoStartFrame >= 0 && m_Frame >= m_AutoStartFrame && m_Run != nullptr) {
        const char *a = std::getenv("SK_TITLE");
        const std::string s = (a != nullptr) ? a : "start";
        if (s == "settings") {
            m_Run->GoToSettings();
        } else if (s == "keybinds") {
            m_Run->GoToKeybinds();
        } else {
            m_Run->GoToCharacterSelect();
        }
        return;
    }
    if ((Util::Input::IsKeyDown(Util::Keycode::RETURN) ||
         Util::Input::IsKeyDown(Util::Keycode::SPACE)) &&
        m_Run != nullptr) {
        LOG_INFO("TitleScene: start -> hero select");
        m_Run->GoToCharacterSelect(); // deferred Replace from inside Update.
        return;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::S) && m_Run != nullptr) {
        m_Run->GoToSettings();
        return;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::K) && m_Run != nullptr) {
        m_Run->GoToKeybinds();
    }
}

void TitleScene::Render() {
    if (!m_Built) {
        Build(); // lazy: Util::Text/Image need a live GL context.
    }
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen space (identity view)
    m_Renderer.Update();
}

} // namespace Game
