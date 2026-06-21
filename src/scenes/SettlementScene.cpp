#include "scenes/SettlementScene.hpp"

#include <cstdio>
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

#include "data/Strings.hpp"
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

namespace {
std::shared_ptr<Util::GameObject> MakeImg(const std::string &path, glm::vec2 pos,
                                          glm::vec2 scale, float z) {
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::make_shared<Util::Image>(path));
    obj->m_Transform.translation = pos;
    obj->m_Transform.scale = scale;
    obj->SetZIndex(z);
    return obj;
}
} // namespace

void SettlementScene::Build() {
    const std::string root = RESOURCE_DIR;
    const std::string sprites = root + "/sprites/";
    const std::string font = root + "/fonts/cjk.ttc";
    Strings::Load(root);

    const bool victory = (m_Outcome == Outcome::Victory);
    const Util::Color gold{255, 220, 60, 255};
    const Util::Color red{255, 90, 90, 255};
    const Util::Color white{235, 235, 235, 255};
    const Util::Color grey{125, 125, 125, 255};

    // Dynamic settlement drawn on a dark backdrop (project's "Settlement screen.png"
    // is a baked mockup with static numbers + a fixed Chinese title, so it cannot be
    // a live bg). The track + real FLOOR/TIME below mirror the video's structure.
    m_Objects.push_back(MakeText(
        font, 52, Strings::Get(victory ? "settle.victory" : "settle.defeat"),
        {0.0F, 250.0F}, victory ? gold : red));

    // Floor-progress track: kChapterFloors nodes; the reached floor carries a hero
    // marker (the video's "1-1 ... flag" track). reached = floor within the chapter.
    const int reached = m_FloorIndex % kChapterFloors;
    const float x0 = -240.0F;
    const float dx = 120.0F;
    const float ty = 120.0F;
    for (int i = 0; i < kChapterFloors; ++i) {
        const float nx = x0 + dx * static_cast<float>(i);
        m_Objects.push_back(MakeImg(sprites + "box02.png", {nx, ty}, {0.7F, 0.7F}, 10.0F));
        m_Objects.push_back(MakeText(font, 16, "1-" + std::to_string(i + 1),
                                     {nx, ty - 36.0F}, i <= reached ? white : grey));
    }
    m_Objects.push_back(MakeImg(sprites + "c01_4.png",
                                {x0 + dx * static_cast<float>(reached), ty + 40.0F},
                                {1.6F, 1.6F}, 12.0F));

    // Stats: real values only (floor reached + chapter play time); a reward chest.
    const double sec = (m_Run != nullptr) ? m_Run->RunTimeMs() / 1000.0 : 0.0;
    char timebuf[16];
    std::snprintf(timebuf, sizeof(timebuf), "%d:%02d", static_cast<int>(sec) / 60,
                  static_cast<int>(sec) % 60);
    m_Objects.push_back(MakeText(font, 26,
                                 Strings::Get("settle.floor") + "  1-" +
                                     std::to_string(reached + 1),
                                 {-150.0F, -30.0F}, white));
    m_Objects.push_back(MakeText(font, 26,
                                 Strings::Get("settle.time") + "  " + timebuf,
                                 {-150.0F, -80.0F}, white));
    m_Objects.push_back(MakeImg(sprites + "box02.png", {230.0F, -55.0F}, {2.2F, 2.2F}, 20.0F));

    m_Objects.push_back(
        MakeText(font, 18, Strings::Get("settle.hint"), {0.0F, -210.0F}, white));

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
        LOG_INFO("SettlementScene: restart -> title (new run via the full flow)");
        m_Run->ShowTitle(); // back to Title -> HeroSelect -> Talent -> floor 0.
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
