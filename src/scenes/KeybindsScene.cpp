#include "scenes/KeybindsScene.hpp"

#include <array>
#include <cstdlib>
#include <string>
#include <utility>

#include <glm/glm.hpp>

#include "Util/Color.hpp"
#include "Util/Input.hpp"
#include "Util/Keycode.hpp"
#include "Util/Logger.hpp"
#include "Util/Text.hpp"
#include "Util/TransformUtils.hpp"

#include "data/Strings.hpp"
#include "game/RunController.hpp"

namespace Game {
namespace {
// {label_key, value_key} into strings.json (localized in Build via Strings::Get).
const std::array<std::pair<const char *, const char *>, 6> kBinds = {{
    {"keys.move", "keys.move_v"},
    {"keys.aim", "keys.aim_v"},
    {"keys.fire", "keys.fire_v"},
    {"keys.skill", "keys.skill_v"},
    {"keys.switch", "keys.switch_v"},
    {"keys.pause", "keys.pause_v"},
}};

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

void KeybindsScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string font = root + "/fonts/cjk.ttc";
    Strings::Load(root);

    m_Objects.push_back(MakeText(font, 38, Strings::Get("keys.title"), {0.0F, 240.0F},
                                 Util::Color{255, 255, 255, 255}, 20.0F));
    float y = 140.0F;
    for (const auto &b : kBinds) {
        m_Objects.push_back(MakeText(font, 24, Strings::Get(b.first), {-220.0F, y},
                                     Util::Color{235, 235, 235, 255}, 21.0F));
        m_Objects.push_back(MakeText(font, 24, Strings::Get(b.second), {180.0F, y},
                                     Util::Color{255, 220, 60, 255}, 21.0F));
        y -= 56.0F;
    }
    m_Objects.push_back(MakeText(font, 18, Strings::Get("keys.back"), {0.0F, -260.0F},
                                 Util::Color{220, 220, 220, 255}, 20.0F));
    for (const auto &o : m_Objects) {
        m_Renderer.AddChild(o);
    }
    m_Built = true;
}

void KeybindsScene::OnEnter() {
    LOG_INFO("KeybindsScene: Esc back");
    if (const char *a = std::getenv("SK_KEYBINDS")) {
        if (std::string(a) == "back") {
            m_AutoBackFrame = 30;
        }
    }
}

void KeybindsScene::OnExit() { LOG_INFO("KeybindsScene: OnExit"); }

void KeybindsScene::Update(float /*dtMs*/) {
    ++m_Frame;
    const bool autoBack = (m_AutoBackFrame >= 0 && m_Frame >= m_AutoBackFrame);
    if ((Util::Input::IsKeyUp(Util::Keycode::ESCAPE) || autoBack) && m_Run != nullptr) {
        LOG_INFO("KeybindsScene: back -> title");
        m_Run->ShowTitle();
    }
}

void KeybindsScene::Render() {
    if (!m_Built) {
        Build();
    }
    Util::SetActiveViewMatrix(glm::mat4(1.0F));
    m_Renderer.Update();
}

} // namespace Game
