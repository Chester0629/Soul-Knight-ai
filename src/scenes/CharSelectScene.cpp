#include "scenes/CharSelectScene.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <glm/glm.hpp>

#include "Util/Animation.hpp"
#include "Util/Color.hpp"
#include "Util/Image.hpp"
#include "Util/Input.hpp"
#include "Util/Keycode.hpp"
#include "Util/Logger.hpp"
#include "Util/TransformUtils.hpp"

#include "game/RunController.hpp"

namespace Game {
namespace {
constexpr float kThumbScale = 2.0F;
constexpr float kPreviewScale = 5.0F;
constexpr float kThumbY = -150.0F;
constexpr float kThumbSpacing = 80.0F;

// Util::Text("") crashes (see Hud.cpp) -- every label starts non-empty.
std::shared_ptr<Util::Text> MakeText(const std::string &font, int size,
                                     const std::string &text) {
    return std::make_shared<Util::Text>(font, size, text, Util::Color{255, 255, 255, 255});
}

std::shared_ptr<Util::GameObject> Holder(std::shared_ptr<Core::Drawable> drawable,
                                         glm::vec2 pos, float z) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::move(drawable));
    obj->m_Transform.translation = pos;
    obj->SetZIndex(z);
    return obj;
}

float RowStartX(std::size_t count) {
    return -0.5F * kThumbSpacing * static_cast<float>(count > 0 ? count - 1 : 0);
}
} // namespace

void CharSelectScene::OnEnter() {
    m_Data.LoadAll(RESOURCE_DIR);
    std::vector<std::string> ids;
    for (const CharacterDef &c : m_Data.Characters()) {
        ids.push_back(c.id);
    }
    if (ids.empty()) {
        ids.push_back("c01"); // no roster table: still selectable
    }
    m_Model = CharSelectModel(ids);
    if (m_Run != nullptr) {
        m_Model.Select(m_Run->State().charId); // resume the last pick
    }
    if (const char *c = std::getenv("SK_CHAR"); c != nullptr && m_Model.Select(c)) {
        m_AutoStartFrame = 30;
    }
    LOG_INFO("CharSelectScene: {} heroes -- A/D or arrows: choose, Enter/Space/click: "
             "start, Esc: menu",
             ids.size());
}

void CharSelectScene::OnExit() {
    LOG_INFO("CharSelectScene: OnExit");
}

void CharSelectScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string font = root + "/fonts/pixel_bold.ttf";

    m_Renderer.AddChild(Holder(MakeText(font, 28, "CHOOSE YOUR HERO"), {0.0F, 300.0F}, 50.0F));

    // The roster as a row of every hero's REAL idle frame (sprite manifest).
    const float x0 = RowStartX(m_Model.Count());
    for (std::size_t i = 0; i < m_Model.Count(); ++i) {
        const auto frames = m_Data.SpriteFrames(m_Model.At(i), root);
        const std::string png = frames.empty() ? root + "/sprites/c01_4.png" : frames.front();
        auto thumb = Holder(std::make_shared<Util::Image>(png),
                            {x0 + kThumbSpacing * static_cast<float>(i), kThumbY}, 40.0F);
        thumb->m_Transform.scale = {kThumbScale, kThumbScale};
        m_Thumbs.push_back(thumb);
        m_Renderer.AddChild(thumb);
    }
    m_Marker = Holder(MakeText(font, 22, "^"), {x0, kThumbY - 40.0F}, 45.0F);
    m_Renderer.AddChild(m_Marker);

    // The selected hero, large and animated, with name + real base stats.
    m_Preview = Holder(std::make_shared<Util::Image>(root + "/sprites/c01_4.png"),
                       {0.0F, 90.0F}, 45.0F);
    m_Preview->m_Transform.scale = {kPreviewScale, kPreviewScale};
    m_Renderer.AddChild(m_Preview);
    m_NameText = MakeText(font, 26, "-");
    m_StatsText = MakeText(font, 16, "-");
    m_SkillText = MakeText(font, 14, "-");
    m_IndexText = MakeText(font, 14, "-");
    m_Renderer.AddChild(Holder(m_NameText, {0.0F, 10.0F}, 50.0F));
    m_Renderer.AddChild(Holder(m_StatsText, {0.0F, -30.0F}, 50.0F));
    m_Renderer.AddChild(Holder(m_SkillText, {0.0F, -60.0F}, 50.0F));
    m_Renderer.AddChild(Holder(m_IndexText, {0.0F, kThumbY - 75.0F}, 50.0F));
    m_Renderer.AddChild(Holder(
        MakeText(font, 14, "A/D or arrows: choose     Enter / Space / click: start     Esc: menu"),
        {0.0F, -300.0F}, 50.0F));

    m_Built = true;
    Refresh();
}

void CharSelectScene::Refresh() {
    const std::string root = RESOURCE_DIR;
    const std::string &id = m_Model.Current();

    const auto frames = m_Data.SpriteFrames(id, root);
    if (!frames.empty()) {
        m_Preview->SetDrawable(std::make_shared<Util::Animation>(
            frames, true, m_Data.SpriteIntervalMs(id, 125), true));
    }
    m_Marker->m_Transform.translation.x =
        RowStartX(m_Model.Count()) + kThumbSpacing * static_cast<float>(m_Model.Index());

    const CharacterDef *c = m_Data.FindCharacter(id);
    const std::string name = (c != nullptr && !c->name.empty()) ? c->name : id;
    m_NameText->SetText(name + "  (" + id + ")");
    if (c != nullptr) {
        m_StatsText->SetText("HP " + std::to_string(c->maxHp) + "   ARMOR " +
                             std::to_string(c->maxArmor) + "   ENERGY " +
                             std::to_string(c->maxEnergy) + "   ATK " +
                             std::to_string(c->atk) + "   CRIT " +
                             std::to_string(c->critical) + "%");
        char buf[96];
        std::snprintf(buf, sizeof buf, "skill cooldown %.1fs   skill duration %.1fs",
                      static_cast<double>(c->skillCd), static_cast<double>(c->inSkillTime));
        m_SkillText->SetText(buf);
    }
    m_IndexText->SetText(std::to_string(m_Model.Index() + 1) + " / " +
                         std::to_string(m_Model.Count()));
}

void CharSelectScene::Update(float /*dtMs*/) {
    ++m_Frame;
    bool changed = false;
    if (Util::Input::IsKeyDown(Util::Keycode::A) || Util::Input::IsKeyDown(Util::Keycode::LEFT)) {
        m_Model.Prev();
        changed = true;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::D) || Util::Input::IsKeyDown(Util::Keycode::RIGHT)) {
        m_Model.Next();
        changed = true;
    }
    if (changed && m_Built) {
        Refresh();
    }

    const bool autoStart = (m_AutoStartFrame >= 0 && m_Frame >= m_AutoStartFrame);
    const bool start = Util::Input::IsKeyDown(Util::Keycode::RETURN) ||
                       Util::Input::IsKeyDown(Util::Keycode::SPACE) ||
                       Util::Input::IsKeyDown(Util::Keycode::MOUSE_LB) || autoStart;
    if (start && m_Run != nullptr) {
        LOG_INFO("CharSelectScene: start run as {}", m_Model.Current());
        m_Run->StartRun(m_Model.Current()); // deferred Replace into floor 0
        return;
    }
    if (Util::Input::IsKeyUp(Util::Keycode::ESCAPE) && m_Run != nullptr) {
        LOG_INFO("CharSelectScene: back -> menu");
        m_Run->ShowMenu();
    }
}

void CharSelectScene::Render() {
    if (!m_Built) {
        Build(); // lazy: Util::Text / Util::Image need a live GL context.
    }
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen space
    m_Renderer.Update();
}

} // namespace Game
