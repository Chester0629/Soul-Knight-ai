#include "ui/Hud.hpp"

#include <string>

#include "Util/Color.hpp"
#include "Util/Image.hpp"
#include "Util/Logger.hpp"

#include "ui/HudLayout.hpp"

namespace Game {
namespace {

using Game::UI::FractionFillLeftAnchor;
using Game::UI::LayoutDoc;
using Game::UI::LayoutNode;
using Game::UI::ParkedDelta;
using Game::UI::ScreenRectToPtsd;
using Game::UI::ShiftRect;
using Game::UI::Vec2;

// Known on-screen anchor for the PARKED `state_bar` group: top-left, 6px inset.
// The static prefab parks state_bar off-screen ABOVE (top-anchored, +aPos.y), so
// its absolute rect is NOT the final position; we anchor the `bg` reference child
// here (working-project / reference-screenshot ground truth, HUD_BUILD_PLAN.md
// s2.5b/s2.6b) and rigid-shift the rest of the group by the same delta. Per-bar
// relative structure stays faithful; only the group's canvas anchor is overridden.
constexpr Vec2 kBgTopLeft{6.0F, 6.0F};

// HUD draw layers. PTSD z must be in (-100,100); glm::ortho(near=-100,far=100)
// negates z so LARGER z = nearer = on top (matches the world's floor 0 < wall 1
// < actors 5 < effects 7, and the working project's panel < fill < text). These
// sit above any world z, so the HUD is always on top of the scene.
constexpr float kZPanel = 99.50F;
constexpr float kZFill = 99.51F;
constexpr float kZText = 99.52F;
// A4 skill button group: ring (bottom) < icon < cooldown mask (top).
constexpr float kZSkillRing = 99.55F;
constexpr float kZSkillIcon = 99.56F;
constexpr float kZSkillMask = 99.57F;

// White numeric readout in the game's pixel UI font; size fits the ~22px bar.
constexpr int kNumberFontSize = 16;

} // namespace

float Hud::Fraction(int cur, int maxValue) {
    if (maxValue <= 0) {
        return 0.0F;
    }
    const float f = static_cast<float>(cur) / static_cast<float>(maxValue);
    return f < 0.0F ? 0.0F : (f > 1.0F ? 1.0F : f);
}

void Hud::Build(const CombatStats &player) {
    const std::string root = RESOURCE_DIR;

    // Phase-1 loader over the baked screen-space layout (same file the
    // HudLayoutTest reads). docs/ sits one level above Resources/.
    // NOTE (release path): this is now a LIVE-render dependency, not just a
    // test/tool one. The Debug RESOURCE_DIR always resolves to the source-tree
    // docs/, but the relative/release RESOURCE_DIR path (CMakeLists FATAL_ERROR
    // gate) must stage Canvas.layout.json beside the binary (or move it under
    // Resources/) before a shipped build, or the HUD silently no-ops (see the
    // graceful !ok() guard below).
    const LayoutDoc doc =
        LayoutDoc::Load(root + "/../docs/layout/Canvas.layout.json");
    if (!doc.ok()) {
        LOG_ERROR("Hud: Canvas.layout.json not loadable -- HUD disabled this run.");
        return; // m_Ready stays false -> Draw() is a no-op (no crash).
    }

    const LayoutNode *bg = doc.Find("state_bar/bg");
    const LayoutNode *hp = doc.Find("state_bar/hp_bar/img");
    const LayoutNode *armor = doc.Find("state_bar/armor_bar/img");
    const LayoutNode *energy = doc.Find("state_bar/energy_bar/img");
    if (bg == nullptr || hp == nullptr || armor == nullptr || energy == nullptr ||
        bg->visual.empty() || hp->visual.empty() || armor->visual.empty() ||
        energy->visual.empty()) {
        LOG_ERROR("Hud: state_bar subtree incomplete -- HUD disabled this run.");
        return;
    }

    // state_bar is PARKED off-screen-above; anchor `bg` to the known top-left and
    // rigid-shift every element by the same screen-space delta. Each element is
    // then converted to its own ABSOLUTE PTSD transform (no container move).
    const Vec2 delta = ParkedDelta(bg->rect, kBgTopLeft);

    // --- bg panel: ui_15 (native 79x39 -> ~247x122 slot), Simple stretch. ---
    {
        const auto &v = bg->visual[0];
        m_Bg = std::make_shared<Util::GameObject>();
        m_Bg->SetDrawable(
            std::make_shared<Util::Image>(root + "/sprites/ui_15.png"));
        m_Bg->m_Transform = ScreenRectToPtsd(ShiftRect(bg->rect, delta), v.texW, v.texH);
        m_Bg->SetZIndex(kZPanel);
        m_Renderer.AddChild(m_Bg);
    }

    // --- three vitals bars. One ui_12 fill (64x7 native) per bar, PRE-tinted in
    //     the PNG (PTSD Base.frag has no color uniform, so the per-bar tint is
    //     baked: hp red / armor grey / energy blue). The number is the current
    //     value (cur), centered on the bar slot. ---
    struct Spec {
        Bar *bar;
        const LayoutNode *node;
        const char *png; // Phase-2 baked tinted PNG (NOT the JSON's untinted crop)
        int cur;
    };
    const Spec specs[] = {
        {&m_Hp, hp, "/sprites/ui_12_hp.png", player.hp},
        {&m_Armor, armor, "/sprites/ui_12_armor.png", player.armor},
        {&m_Energy, energy, "/sprites/ui_12_energy.png", player.energy},
    };
    const Util::Color white{255, 255, 255, 255};
    const std::string font = root + "/fonts/pixel_bold.ttf";
    for (const Spec &s : specs) {
        const auto &v = s.node->visual[0];
        s.bar->texW = v.texW; // 64 (ui_12 native) -- the fill-compensation basis
        s.bar->full = ScreenRectToPtsd(ShiftRect(s.node->rect, delta), v.texW, v.texH);

        s.bar->fill = std::make_shared<Util::GameObject>();
        s.bar->fill->SetDrawable(std::make_shared<Util::Image>(root + s.png));
        s.bar->fill->m_Transform = s.bar->full; // replaced each frame by the fraction
        s.bar->fill->SetZIndex(kZFill);
        m_Renderer.AddChild(s.bar->fill);

        // std::to_string(int) is never empty, so the Util::Text("") crash cannot
        // occur; still seed with the real value so the first frame is correct.
        s.bar->text = std::make_shared<Util::Text>(font, kNumberFontSize,
                                                    std::to_string(s.cur), white);
        s.bar->textObj = std::make_shared<Util::GameObject>();
        s.bar->textObj->SetDrawable(s.bar->text);
        s.bar->textObj->m_Transform.translation = s.bar->full.translation; // bar center
        s.bar->textObj->SetZIndex(kZText);
        m_Renderer.AddChild(s.bar->textObj);
    }

    // --- DEFERRED (explicitly NOT drawn this phase) ---
    // warn icons (`ui_62`, native 4x10) and `badass_mode` (`ui_11`, native 20x17)
    // are conditional indicators, not always-on HUD: in the static prefab the
    // three `state_bar/<bar>/warn` nodes are active:false (shown only when that
    // vital is low, with a blink), and badass_mode is a berserk-state toggle. The
    // port has no low-value-blink timer nor a badass state to drive them, so
    // rendering them now (statically visible) would fabricate behaviour the source
    // does not show. Deferred with their prefab data recorded for a later phase:
    //   warn:  state_bar/{hp,armor,energy}_bar/warn, ui_62, tints
    //          (1.0,0.03,0.03)/(0.6,0.6,0.6)/(0.03,0.48,1.0), active:false.
    //   badass: state_bar/badass_mode, ui_11, rect L=318.8 T=-106.6 62.5x53.1.

    // --- A4: skill button group (ring + icon + Vertical/Bottom Filled cooldown mask),
    // anchored bottom-right. Rects from Canvas.layout.json (control/btn_skill subtree);
    // the static screen_rect is off-canvas (top 940 on a 720 canvas -- the same offset
    // state_bar carries), so we anchor btn_skill to a known on-screen bottom-right
    // target and rigid-shift the group, exactly like the state_bar parked handling. ---
    using Game::UI::ScreenRect;
    const ScreenRect ringRect{1120.0F, 940.0F, 120.0F, 120.0F};  // ui_joy_0 (Canvas.layout.json:1354)
    const ScreenRect iconRect{1159.3F, 964.06F, 41.41F, 71.88F}; // ui_joy_2 (:1429)
    const ScreenRect maskRect{1159.3F, 964.45F, 41.41F, 71.09F}; // ui_joy_3 (:1504)
    const Vec2 kSkillTopLeft{1136.0F, 576.0F};                   // 120x120 button, ~24px corner margin
    const Vec2 skillDelta = ParkedDelta(ringRect, kSkillTopLeft);

    m_SkillRing = std::make_shared<Util::GameObject>();
    m_SkillRing->SetDrawable(std::make_shared<Util::Image>(root + "/sprites/ui_joy_0.png"));
    m_SkillRing->m_Transform = ScreenRectToPtsd(ShiftRect(ringRect, skillDelta), 227.0F, 227.0F);
    m_SkillRing->SetZIndex(kZSkillRing);
    m_Renderer.AddChild(m_SkillRing);

    m_SkillIcon = std::make_shared<Util::GameObject>();
    m_SkillIcon->SetDrawable(std::make_shared<Util::Image>(root + "/sprites/ui_joy_2.png"));
    m_SkillIcon->m_Transform = ScreenRectToPtsd(ShiftRect(iconRect, skillDelta), 53.0F, 91.92388F);
    m_SkillIcon->SetZIndex(kZSkillIcon);
    m_Renderer.AddChild(m_SkillIcon);

    // The cooldown mask: a PRE-TINTED cyan PNG (Base.frag has no tint uniform), drawn
    // by the UV-crop FilledImage. Fraction is set each frame in Draw().
    m_SkillMask = std::make_shared<Util::FilledImage>(root + "/sprites/ui_joy_3_cyan.png");
    m_SkillMaskObj = std::make_shared<Util::GameObject>();
    m_SkillMaskObj->SetDrawable(m_SkillMask);
    m_SkillMaskObj->m_Transform = ScreenRectToPtsd(ShiftRect(maskRect, skillDelta), 53.0F, 91.0F);
    m_SkillMaskObj->SetZIndex(kZSkillMask);
    m_Renderer.AddChild(m_SkillMaskObj);

    m_Ready = true;
}

void Hud::Draw(const CombatStats &player) {
    if (!m_Built) {
        Build(player);  // one-shot lazy init (needs a live GL context).
        m_Built = true; // attempted -- never retried (avoids per-frame reload).
    }
    if (!m_Ready) {
        return; // layout/assets unavailable -> draw nothing.
    }

    // Per frame: left-anchored fill fraction + numeric readout per bar. A damaged
    // bar shrinks from the right (left edge fixed) via FractionFillLeftAnchor.
    const struct {
        Bar *bar;
        int cur;
        int maxValue;
    } updates[] = {
        {&m_Hp, player.hp, player.maxHp},
        {&m_Armor, player.armor, player.maxArmor},
        {&m_Energy, player.energy, player.maxEnergy},
    };
    for (const auto &u : updates) {
        u.bar->fill->m_Transform =
            FractionFillLeftAnchor(u.bar->full, Fraction(u.cur, u.maxValue), u.bar->texW);
        // Only re-rasterize the glyph + re-upload the GL texture when the value
        // actually changed (SetText -> TTF_RenderUTF8 + texture upload every call).
        if (u.cur != u.bar->lastValue) {
            u.bar->text->SetText(std::to_string(u.cur)); // int -> never empty
            u.bar->lastValue = u.cur;
        }
    }

    // A4: drive the cooldown mask from the skill cooldown progress (0..1, 1=ready).
    // Per UI_BEHAVIOR_SPEC sec.3a (UpdateSkillMask: fillAmount = elapsed/total), this IS
    // skillCdProgress directly -- the cyan fills bottom-up as the skill recharges
    // (empty just-cast -> full when ready; active stays 1.0 -> full, no cooldown shown).
    if (m_SkillMask != nullptr) {
        m_SkillMask->SetFraction(player.skillCdProgress);
    }

    m_Renderer.Update(); // caller already set the identity (screen-space) view.
}

} // namespace Game
