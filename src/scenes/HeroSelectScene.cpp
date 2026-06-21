#include "scenes/HeroSelectScene.hpp"

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
constexpr float kBaseScale = 3.5F;  // unselected hero sprite scale.
constexpr float kPickScale = 5.5F;  // selected hero sprite scale.

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

void HeroSelectScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string sprites = root + "/sprites/";
    const std::string font = root + "/fonts/pixel_bold.ttf";

    // Roster: only c01/c02 have faithful skill brains today; c03 is a stub but
    // playable (gate+cooldown). Easy to extend with more ids/names + sprites.
    m_CharIds = {"c01", "c02", "c03"};
    m_Names = {"KNIGHT", "ROGUE", "WIZARD"};
    const float xs[] = {-340.0F, 0.0F, 340.0F};

    // Room backdrop (the hub bg, scaled to roughly fill the 1280x720 screen).
    {
        auto bg = std::make_shared<Util::GameObject>();
        auto img = std::make_shared<Util::Image>(sprites + "common_room_bg.png");
        const glm::vec2 sz = img->GetSize();
        bg->SetDrawable(img);
        if (sz.x > 0 && sz.y > 0) {
            bg->m_Transform.scale = glm::vec2(1280.0F / sz.x, 720.0F / sz.y);
        }
        bg->SetZIndex(0.0F);
        m_Renderer.AddChild(bg);
    }

    m_Renderer.AddChild(MakeText(font, 34, "SELECT HERO", {0.0F, 260.0F},
                                 Util::Color{255, 255, 255, 255}, 20.0F));

    for (std::size_t i = 0; i < m_CharIds.size(); ++i) {
        auto obj = std::make_shared<Util::GameObject>();
        obj->SetDrawable(std::make_shared<Util::Image>(sprites + m_CharIds[i] + "_4.png"));
        obj->m_Transform.translation = {xs[i], 30.0F};
        obj->m_Transform.scale = glm::vec2(kBaseScale, kBaseScale);
        obj->SetZIndex(8.0F);
        m_CharObjs.push_back(obj);
        m_Renderer.AddChild(obj);
    }

    m_NameText = std::make_shared<Util::Text>(font, 28, m_Names[0],
                                              Util::Color{255, 220, 60, 255});
    auto nameObj = std::make_shared<Util::GameObject>();
    nameObj->SetDrawable(m_NameText);
    nameObj->m_Transform.translation = {0.0F, -150.0F};
    nameObj->SetZIndex(20.0F);
    m_Renderer.AddChild(nameObj);

    m_Renderer.AddChild(MakeText(font, 18, "A/D: switch    ENTER: confirm    ESC: back",
                                 {0.0F, -280.0F}, Util::Color{220, 220, 220, 255}, 20.0F));
    m_Built = true;
}

void HeroSelectScene::ApplyHighlight() {
    if (m_Sel == m_LastApplied) {
        return;
    }
    for (std::size_t i = 0; i < m_CharObjs.size(); ++i) {
        const float s = (static_cast<int>(i) == m_Sel) ? kPickScale : kBaseScale;
        m_CharObjs[i]->m_Transform.scale = glm::vec2(s, s);
    }
    if (m_NameText != nullptr) {
        m_NameText->SetText(m_Names[static_cast<std::size_t>(m_Sel)]);
    }
    m_LastApplied = m_Sel;
}

void HeroSelectScene::OnEnter() {
    LOG_INFO("HeroSelectScene: A/D switch, Enter confirm, Esc back");
    // SK_SELECT=cNN test hook (env, NO-OP if unset): select that hero + confirm.
    if (const char *sel = std::getenv("SK_SELECT")) {
        m_AutoConfirmFrame = 30; // selection applied in Update once built.
    }
}

void HeroSelectScene::OnExit() { LOG_INFO("HeroSelectScene: OnExit"); }

void HeroSelectScene::Update(float /*dtMs*/) {
    ++m_Frame;
    if (!m_Built) {
        return; // wait for the lazy Build (first Render) before input.
    }

    const int n = static_cast<int>(m_CharIds.size());
    if (Util::Input::IsKeyDown(Util::Keycode::A) ||
        Util::Input::IsKeyDown(Util::Keycode::LEFT)) {
        m_Sel = (m_Sel - 1 + n) % n;
    }
    if (Util::Input::IsKeyDown(Util::Keycode::D) ||
        Util::Input::IsKeyDown(Util::Keycode::RIGHT)) {
        m_Sel = (m_Sel + 1) % n;
    }
    if (Util::Input::IsKeyUp(Util::Keycode::ESCAPE) && m_Run != nullptr) {
        LOG_INFO("HeroSelectScene: back -> title");
        m_Run->ShowTitle();
        return;
    }

    // Headless: force a specific hero (SK_SELECT=cNN) then auto-confirm.
    bool autoConfirm = false;
    if (m_AutoConfirmFrame >= 0 && m_Frame >= m_AutoConfirmFrame) {
        if (const char *sel = std::getenv("SK_SELECT")) {
            for (int i = 0; i < n; ++i) {
                if (m_CharIds[static_cast<std::size_t>(i)] == sel) {
                    m_Sel = i;
                }
            }
        }
        autoConfirm = true;
    }

    if ((Util::Input::IsKeyDown(Util::Keycode::RETURN) ||
         Util::Input::IsKeyDown(Util::Keycode::SPACE) || autoConfirm) &&
        m_Run != nullptr) {
        const std::string &id = m_CharIds[static_cast<std::size_t>(m_Sel)];
        LOG_INFO("HeroSelectScene: confirm hero '{}' -> begin run", id);
        m_Run->BeginRun(id); // deferred Replace into floor 0.
    }
}

void HeroSelectScene::Render() {
    if (!m_Built) {
        Build(); // lazy: Util::Text/Image need a live GL context.
    }
    ApplyHighlight();
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen space (identity view)
    m_Renderer.Update();
}

} // namespace Game
