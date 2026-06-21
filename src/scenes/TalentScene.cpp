#include "scenes/TalentScene.hpp"

#include <array>
#include <cstdlib>
#include <string>

#include <glm/glm.hpp>

#include "Util/Color.hpp"
#include "Util/Input.hpp"
#include "Util/Keycode.hpp"
#include "Util/Logger.hpp"
#include "Util/Text.hpp"
#include "Util/TransformUtils.hpp"

#include "game/RunController.hpp"

namespace Game {
namespace {
struct TalentOption {
    const char *label;
    int dHp;
    int dArmor;
    int dEnergy;
};
constexpr std::array<TalentOption, 3> kTalents = {{
    {"HP +4", 4, 0, 0},
    {"ARMOR +1", 0, 1, 0},
    {"ENERGY +100", 0, 0, 100},
}};
constexpr float kBaseScale = 1.0F;
constexpr float kPickScale = 1.35F;

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

void TalentScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string font = root + "/fonts/pixel_bold.ttf";
    const float xs[] = {-360.0F, 0.0F, 360.0F};

    m_Renderer.AddChild(MakeText(font, 40, "CHOOSE TALENT", {0.0F, 220.0F},
                                 Util::Color{255, 255, 255, 255}, 20.0F));
    for (std::size_t i = 0; i < kTalents.size(); ++i) {
        auto card = MakeText(font, 26, kTalents[i].label, {xs[i], 0.0F},
                             Util::Color{255, 220, 60, 255}, 21.0F);
        m_Cards.push_back(card);
        m_Renderer.AddChild(card);
    }
    m_Renderer.AddChild(MakeText(font, 18,
                                 "A/D or 1/2/3: choose     ENTER: confirm",
                                 {0.0F, -240.0F}, Util::Color{220, 220, 220, 255}, 20.0F));
    m_Built = true;
}

void TalentScene::ApplyHighlight() {
    if (m_Sel == m_LastApplied) {
        return;
    }
    for (std::size_t i = 0; i < m_Cards.size(); ++i) {
        const float s = (static_cast<int>(i) == m_Sel) ? kPickScale : kBaseScale;
        m_Cards[i]->m_Transform.scale = glm::vec2(s, s);
    }
    m_LastApplied = m_Sel;
}

void TalentScene::Confirm() {
    if (m_Run == nullptr) {
        return;
    }
    const TalentOption &t = kTalents[static_cast<std::size_t>(m_Sel)];
    LOG_INFO("TalentScene: chose '{}' (hp+{} armor+{} energy+{})", t.label, t.dHp,
             t.dArmor, t.dEnergy);
    m_Run->ChooseTalent(t.dHp, t.dArmor, t.dEnergy); // deferred Replace into floor 0.
}

void TalentScene::OnEnter() {
    LOG_INFO("TalentScene: A/D or 1/2/3 choose, Enter confirm");
    if (const char *n = std::getenv("SK_TALENT")) {
        m_Sel = std::atoi(n);
        if (m_Sel < 0 || m_Sel >= static_cast<int>(kTalents.size())) {
            m_Sel = 0;
        }
        m_AutoConfirmFrame = 30;
    }
}

void TalentScene::OnExit() { LOG_INFO("TalentScene: OnExit"); }

void TalentScene::Update(float /*dtMs*/) {
    ++m_Frame;
    if (!m_Built) {
        return;
    }
    const int n = static_cast<int>(kTalents.size());
    if (Util::Input::IsKeyDown(Util::Keycode::A) ||
        Util::Input::IsKeyDown(Util::Keycode::LEFT)) {
        m_Sel = (m_Sel - 1 + n) % n;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::D) ||
        Util::Input::IsKeyDown(Util::Keycode::RIGHT)) {
        m_Sel = (m_Sel + 1) % n;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::NUM_1)) {
        m_Sel = 0;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::NUM_2)) {
        m_Sel = 1;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::NUM_3)) {
        m_Sel = 2;
    }

    const bool autoConfirm = (m_AutoConfirmFrame >= 0 && m_Frame >= m_AutoConfirmFrame);
    if (Util::Input::IsKeyDown(Util::Keycode::RETURN) ||
        Util::Input::IsKeyDown(Util::Keycode::SPACE) || autoConfirm) {
        Confirm();
    }
}

void TalentScene::Render() {
    if (!m_Built) {
        Build();
    }
    ApplyHighlight();
    Util::SetActiveViewMatrix(glm::mat4(1.0F));
    m_Renderer.Update();
}

} // namespace Game
