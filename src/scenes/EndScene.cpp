#include "scenes/EndScene.hpp"

#include <cstdlib>
#include <string>
#include <utility>

#include <glm/glm.hpp>

#include "Core/Context.hpp"

#include "Util/Animation.hpp"
#include "Util/Color.hpp"
#include "Util/Image.hpp"
#include "Util/Input.hpp"
#include "Util/Keycode.hpp"
#include "Util/Logger.hpp"
#include "Util/Text.hpp"
#include "Util/TransformUtils.hpp"

#include "game/RunController.hpp"
#include "game/RunState.hpp"

namespace Game {
namespace {
// Screen-space GameObject: centre-origin coords, +y up (TransformUtils:
// translation {0,0} is the screen centre). Util::Text("") crashes (see Hud.cpp),
// so every label is non-empty.
std::shared_ptr<Util::GameObject> Holder(std::shared_ptr<Core::Drawable> drawable,
                                         glm::vec2 pos, float scale, float z = 50.0F) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::move(drawable));
    obj->m_Transform.translation = pos;
    obj->m_Transform.scale = {scale, scale};
    obj->SetZIndex(z);
    return obj;
}

std::shared_ptr<Util::GameObject> Text(const std::string &font, int size,
                                       const std::string &text, glm::vec2 pos) {
    return Holder(std::make_shared<Util::Text>(font, size, text.empty() ? "-" : text,
                                               Util::Color{255, 255, 255, 255}),
                  pos, 1.0F, 55.0F);
}

std::shared_ptr<Util::GameObject> Sprite(const std::string &root, const char *png,
                                         glm::vec2 pos, float scale, float z = 45.0F) {
    return Holder(std::make_shared<Util::Image>(root + "/sprites/" + png), pos, scale, z);
}

// Statements.unity draws its ~20px UI icons in ~62px rects (3x), the ui2_0
// portrait frame 28->112 (4x), the ui_38 OK button 43->240 (~5.5x) and the ui_75
// divider 76->762 (10x). Same ratios here, on the same 1280x720 canvas.
constexpr float kIconScale = 3.0F;
constexpr float kFrameScale = 4.0F;
constexpr float kButtonScale = 5.5F;
constexpr float kDividerScale = 10.0F;
constexpr float kHeroScale = 3.0F;
} // namespace

void EndScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string font = root + "/fonts/pixel_bold.ttf";
    const RunState &st = m_Run != nullptr ? m_Run->State() : RunState{};
    const RunStats &s = st.stats;

    // Title: i2 I_gameover / I_victory (English slot) -- UIStatement.title_text.
    const bool victory = st.outcome == RunState::Outcome::Victory;
    m_Objects.push_back(Text(font, 56, victory ? "VICTORY" : "GAME OVER", {0.0F, 270.0F}));
    // UIStatement.level_text <- level_list[this_index]: where the run ended.
    m_Objects.push_back(Text(font, 40, LevelLabel(st.floorIndex), {0.0F, 205.0F}));

    // Hero portrait (Statements 'player' image in the ui2_0 frame, left side).
    m_Objects.push_back(Sprite(root, "ui2_0.png", {-375.0F, 40.0F}, kFrameScale, 40.0F));
    const auto frames = m_Data.SpriteFrames(st.charId, root);
    if (!frames.empty()) {
        m_Objects.push_back(Holder(
            std::make_shared<Util::Animation>(frames, true, m_Data.SpriteIntervalMs(st.charId, 125), true),
            {-375.0F, 44.0F}, kHeroScale, 46.0F));
    }
    const CharacterDef *c = m_Data.FindCharacter(st.charId);
    m_Objects.push_back(Text(font, 18, (c != nullptr && !c->name.empty()) ? c->name : st.charId,
                             {-375.0F, -40.0F}));

    // Counter row (Statements 'info': time / kill / coin icons, each with its number).
    const float rowY = 70.0F;
    m_Objects.push_back(Sprite(root, "ui_10.png", {-170.0F, rowY}, kIconScale));
    m_Objects.push_back(Text(font, 30, FormatRunClock(s.timeMs), {-40.0F, rowY}));
    m_Objects.push_back(Sprite(root, "ui_11.png", {110.0F, rowY}, kIconScale));
    m_Objects.push_back(Text(font, 30, std::to_string(s.kills), {180.0F, rowY}));
    m_Objects.push_back(Sprite(root, "ui_5.png", {290.0F, rowY}, kIconScale));
    m_Objects.push_back(Text(font, 30, std::to_string(s.coins), {360.0F, rowY}));
    m_Objects.push_back(Sprite(root, "ui_75.png", {80.0F, rowY - 55.0F}, kDividerScale, 40.0F));

    // Gems earned this run (UIStatement.text_this_gems "+N" beside the ui_52 gem).
    m_Objects.push_back(Sprite(root, "ui_52.png", {-40.0F, -30.0F}, kIconScale));
    m_Objects.push_back(Text(font, 36, "+" + std::to_string(s.gemsThisRun), {60.0F, -30.0F}));

    // OK button (Statements btn_ok, ui_38) + the port's key legend.
    m_Objects.push_back(Sprite(root, "ui_38.png", {0.0F, -170.0F}, kButtonScale));
    m_Objects.push_back(Text(font, 22, "OK", {0.0F, -170.0F}));
    m_Objects.push_back(Text(font, 14,
                             "Enter / Space / click / Esc: OK (menu)     R: try again     Q: quit",
                             {0.0F, -300.0F}));

    for (const auto &o : m_Objects) {
        m_Renderer.AddChild(o);
    }
    m_Built = true;
}

void EndScene::OnEnter() {
    m_Data.LoadAll(RESOURCE_DIR);
    if (m_Run != nullptr) {
        const RunState &st = m_Run->State();
        LOG_INFO("EndScene: {} at {} -- hero {} time {} kills {} coins {} gems +{}",
                 st.outcome == RunState::Outcome::Victory ? "victory" : "game over",
                 LevelLabel(st.floorIndex), st.charId, FormatRunClock(st.stats.timeMs),
                 st.stats.kills, st.stats.coins, st.stats.gemsThisRun);
    }
    LOG_INFO("EndScene: Enter/Space/click/Esc: menu, R: try again, Q: quit");
    // SK_ENDSCENE=restart|quit|menu test hook (env, NO-OP if unset): auto-pick the
    // action after a few frames, so the death->EndScene->... path runs headlessly.
    if (const char *act = std::getenv("SK_ENDSCENE")) {
        const std::string a = act;
        if (a == "restart") {
            m_AutoAction = AutoAction::Restart;
        } else if (a == "quit") {
            m_AutoAction = AutoAction::Quit;
        } else if (a == "menu") {
            m_AutoAction = AutoAction::Menu;
        }
        if (m_AutoAction != AutoAction::None) {
            m_AutoActionFrame = 30;
        }
    }
}

void EndScene::OnExit() {
    LOG_INFO("EndScene: OnExit (left the results screen)");
}

void EndScene::Update(float /*dtMs*/) {
    ++m_Frame;
    const bool autoFire = (m_AutoActionFrame >= 0 && m_Frame >= m_AutoActionFrame);

    const bool restart = Util::Input::IsKeyDown(Util::Keycode::R) ||
                         (autoFire && m_AutoAction == AutoAction::Restart);
    const bool quit = Util::Input::IsKeyDown(Util::Keycode::Q) || Util::Input::IfExit() ||
                      (autoFire && m_AutoAction == AutoAction::Quit);
    // FAITHFUL: UIStatement.Update -- Submit / Cancel / Escape all == OnBtnClick.
    const bool ok = Util::Input::IsKeyDown(Util::Keycode::RETURN) ||
                    Util::Input::IsKeyDown(Util::Keycode::SPACE) ||
                    Util::Input::IsKeyDown(Util::Keycode::MOUSE_LB) ||
                    Util::Input::IsKeyUp(Util::Keycode::ESCAPE) ||
                    (autoFire && m_AutoAction == AutoAction::Menu);

    if (restart && m_Run != nullptr) {
        LOG_INFO("EndScene: restart -> new run (floor 0, same hero)");
        m_Run->StartRun(); // Replaces this scene with a fresh floor-0 GameScene.
        return;
    }
    if (ok && m_Run != nullptr) {
        LOG_INFO("EndScene: ok -> main menu");
        m_Run->ShowMenu();
        return;
    }
    if (quit) {
        LOG_INFO("EndScene: quit -> exit");
        Core::Context::GetInstance()->SetExit(true);
    }
}

void EndScene::Render() {
    if (!m_Built) {
        Build(); // lazy: Util::Text / Util::Image need a live GL context.
    }
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen space (identity view)
    m_Renderer.Update();
}

} // namespace Game
