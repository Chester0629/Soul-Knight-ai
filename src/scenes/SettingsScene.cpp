#include "scenes/SettingsScene.hpp"

#include <algorithm>
#include <array>
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

std::array<int, 3> SettingsScene::s_Volumes = {80, 80, 80};

namespace {
constexpr std::array<const char *, 3> kRowLabels = {"MASTER", "BGM", "SFX"};
constexpr float kRowY[3] = {90.0F, 0.0F, -90.0F};
constexpr float kBarX = 120.0F;   // bar centre x.
constexpr float kBarW = 360.0F;   // bar width (px) at 100%.
constexpr float kBarH = 18.0F;

std::shared_ptr<Util::GameObject> MakeText(const std::string &font, int size,
                                           const std::string &text, glm::vec2 pos,
                                           Util::Color color, float z) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::make_shared<Util::Text>(font, size, text, color));
    obj->m_Transform.translation = pos;
    obj->SetZIndex(z);
    return obj;
}
std::shared_ptr<Util::GameObject> MakeBar(const std::string &png, glm::vec2 pos,
                                          float w, float h, float z) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::make_shared<Util::Image>(png));
    obj->m_Transform.translation = pos;
    obj->m_Transform.scale = {w / 64.0F, h / 7.0F}; // ui_12_* are 64x7 native.
    obj->SetZIndex(z);
    return obj;
}
} // namespace

void SettingsScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string sprites = root + "/sprites/";
    const std::string font = root + "/fonts/pixel_bold.ttf";

    m_Renderer.AddChild(MakeText(font, 38, "SETTINGS", {0.0F, 220.0F},
                                 Util::Color{255, 255, 255, 255}, 20.0F));
    for (std::size_t i = 0; i < 3; ++i) {
        m_Renderer.AddChild(MakeText(font, 24, kRowLabels[i], {-360.0F, kRowY[i]},
                                     Util::Color{235, 235, 235, 255}, 21.0F));
        // grey track + blue fill (energy bar sprite reused for the slider).
        m_Renderer.AddChild(MakeBar(sprites + "ui_12_armor.png",
                                    {kBarX, kRowY[i]}, kBarW, kBarH, 21.0F));
        auto fill = MakeBar(sprites + "ui_12_energy.png", {kBarX, kRowY[i]}, kBarW,
                            kBarH, 22.0F);
        m_Fills[i] = fill;
        m_Renderer.AddChild(fill);
    }
    m_Marker = MakeText(font, 24, ">", {-410.0F, kRowY[0]},
                        Util::Color{255, 220, 60, 255}, 23.0F);
    m_Renderer.AddChild(m_Marker);
    m_Renderer.AddChild(MakeText(font, 18,
                                 "W/S: row   A/D: adjust   ESC: back",
                                 {0.0F, -240.0F}, Util::Color{220, 220, 220, 255}, 20.0F));
    m_Built = true;
}

void SettingsScene::Refresh() {
    if (m_Sel != m_LastSel) {
        m_Marker->m_Transform.translation.y = kRowY[m_Sel];
        m_LastSel = m_Sel;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (s_Volumes[i] == m_LastVals[i] || m_Fills[i] == nullptr) {
            continue;
        }
        const float frac = static_cast<float>(s_Volumes[i]) / 100.0F;
        m_Fills[i]->m_Transform.scale.x = (kBarW * frac) / 64.0F;
        m_Fills[i]->m_Transform.translation.x = kBarX - kBarW / 2.0F + (kBarW * frac) / 2.0F;
        m_LastVals[i] = s_Volumes[i];
    }
}

void SettingsScene::OnEnter() {
    LOG_INFO("SettingsScene: W/S row, A/D adjust, Esc back");
    if (const char *a = std::getenv("SK_SETTINGS")) {
        if (std::string(a) == "back") {
            m_AutoBackFrame = 30;
        }
    }
}

void SettingsScene::OnExit() { LOG_INFO("SettingsScene: OnExit"); }

void SettingsScene::Update(float /*dtMs*/) {
    ++m_Frame;
    if (!m_Built) {
        return;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::W) ||
        Util::Input::IsKeyDown(Util::Keycode::UP)) {
        m_Sel = (m_Sel + 2) % 3;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::S) ||
        Util::Input::IsKeyDown(Util::Keycode::DOWN)) {
        m_Sel = (m_Sel + 1) % 3;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::A) ||
        Util::Input::IsKeyDown(Util::Keycode::LEFT)) {
        s_Volumes[static_cast<std::size_t>(m_Sel)] =
            std::max(0, s_Volumes[static_cast<std::size_t>(m_Sel)] - 10);
    }
    if (Util::Input::IsKeyDown(Util::Keycode::D) ||
        Util::Input::IsKeyDown(Util::Keycode::RIGHT)) {
        s_Volumes[static_cast<std::size_t>(m_Sel)] =
            std::min(100, s_Volumes[static_cast<std::size_t>(m_Sel)] + 10);
    }
    const bool autoBack = (m_AutoBackFrame >= 0 && m_Frame >= m_AutoBackFrame);
    if ((Util::Input::IsKeyUp(Util::Keycode::ESCAPE) || autoBack) && m_Run != nullptr) {
        LOG_INFO("SettingsScene: back -> title");
        m_Run->ShowTitle();
    }
}

void SettingsScene::Render() {
    if (!m_Built) {
        Build();
    }
    Refresh();
    Util::SetActiveViewMatrix(glm::mat4(1.0F));
    m_Renderer.Update();
}

} // namespace Game
