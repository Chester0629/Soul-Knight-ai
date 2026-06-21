#include "scenes/SettlementScene.hpp"

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
#include "game/RunState.hpp"

namespace Game {
namespace {
// Screen-space centred text GameObject (TransformUtils: {0,0} == screen centre).
std::shared_ptr<Util::GameObject> MakeText(const std::string &font, int size,
                                           const std::string &text, glm::vec2 pos,
                                           Util::Color color) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::make_shared<Util::Text>(font, size, text, color));
    obj->m_Transform.translation = pos;
    obj->SetZIndex(50.0F);
    return obj;
}
} // namespace

void SettlementScene::Build() {
    const std::string root = RESOURCE_DIR;
    // pixel_bold.ttf is the EndScene font (ASCII). CJK title + the video's stats
    // panel / floor-track / reward-chest are the visual-polish pass (CJK font + bg).
    const std::string font = root + "/fonts/pixel_bold.ttf";

    const bool victory = (m_Outcome == Outcome::Victory);
    const Util::Color gold{255, 220, 60, 255};
    const Util::Color red{255, 90, 90, 255};
    const Util::Color white{235, 235, 235, 255};

    const int chapter = m_FloorIndex / kChapterFloors + 1;
    const int floorInChapter = m_FloorIndex % kChapterFloors + 1;

    m_Objects.push_back(MakeText(font, 48, victory ? "VICTORY" : "GAME OVER",
                                 {0.0F, 120.0F}, victory ? gold : red));
    m_Objects.push_back(MakeText(
        font, 22,
        victory ? "Chapter " + std::to_string(chapter) + " cleared!"
                : "Fell on floor " + std::to_string(chapter) + "-" +
                      std::to_string(floorInChapter),
        {0.0F, 40.0F}, white));
    m_Objects.push_back(
        MakeText(font, 18, "R: new run     Esc: quit", {0.0F, -60.0F}, white));

    for (const auto &o : m_Objects) {
        m_Renderer.AddChild(o);
    }
    m_Built = true;
}

void SettlementScene::OnEnter() {
    LOG_INFO("SettlementScene: outcome={} floorIndex={} -- R: new run, Esc: quit",
             m_Outcome == Outcome::Victory ? "Victory" : "Defeat", m_FloorIndex);
    // SK_SETTLEMENT=restart|quit test hook (env, NO-OP if unset): auto-pick after a
    // few frames so the clear/death -> Settlement -> restart/quit path runs headlessly.
    if (const char *act = std::getenv("SK_SETTLEMENT")) {
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

void SettlementScene::OnExit() { LOG_INFO("SettlementScene: OnExit (left results)"); }

void SettlementScene::Update(float /*dtMs*/) {
    ++m_Frame;
    const bool autoFire = (m_AutoActionFrame >= 0 && m_Frame >= m_AutoActionFrame);

    const bool restart = Util::Input::IsKeyDown(Util::Keycode::R) ||
                         (autoFire && m_AutoAction == AutoAction::Restart);
    const bool quit = Util::Input::IsKeyUp(Util::Keycode::ESCAPE) ||
                      Util::Input::IsKeyDown(Util::Keycode::Q) || Util::Input::IfExit() ||
                      (autoFire && m_AutoAction == AutoAction::Quit);

    if (restart && m_Run != nullptr) {
        LOG_INFO("SettlementScene: restart -> new run (floor 0)");
        m_Run->StartRun(); // Replaces this scene with a fresh floor-0 GameScene.
        return;
    }
    if (quit) {
        LOG_INFO("SettlementScene: quit -> exit");
        Core::Context::GetInstance()->SetExit(true);
    }
}

void SettlementScene::Render() {
    if (!m_Built) {
        Build(); // lazy: Util::Text needs a live GL context (run from the render loop).
    }
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen space (identity view)
    m_Renderer.Update();
}

} // namespace Game
