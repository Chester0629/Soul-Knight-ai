#ifndef GAME_HUD_HPP
#define GAME_HUD_HPP

#include <memory>

#include "Util/FilledImage.hpp"
#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"
#include "Util/Text.hpp"
#include "Util/Transform.hpp"

#include "combat/CombatStats.hpp"

namespace Game {
/**
 * @class Hud
 * @brief Faithful in-game vitals HUD drawn with PTSD screen-space sprites.
 *
 * HUD landing Phase 3 (a4 wiring). Replaces the old ImGui overlay with the real
 * Soul-Knight HUD: a `ui_15` background panel plus three left-anchored vitals
 * bars (HP / armor / energy) built from one pre-tinted `ui_12` sprite each, with
 * an inline numeric readout per bar.
 *
 * Geometry comes from the verified Phase-1 pipeline (`Game::UI` HudLayout):
 * `Canvas.layout.json` -> `state_bar` subtree -> ParkedDelta anchors the group's
 * `bg` to the on-screen top-left (6,6) -> ScreenRectToPtsd gives each element's
 * ABSOLUTE PTSD transform (no parent/container offset; PTSD does not compose
 * parent->child transforms). Per frame the bar fills use FractionFillLeftAnchor
 * (Phase-2 verified left-anchored LtR fill) so a damaged bar shrinks from the
 * right with its left edge fixed.
 *
 * Render-only: @ref Draw reads a @ref CombatStats snapshot and emits sprites via
 * its own screen-space @ref Util::Renderer. It owns no combat state. Call it once
 * per frame after the scene switches to the identity view matrix
 * (GameScene::Render, screen space).
 */
class Hud {
public:
    Hud() = default;

    /**
     * @brief Update the bars/numbers from @p player and draw the HUD.
     *
     * The first call lazily builds the HUD GameObjects (loads the layout JSON +
     * baked PNGs -- needs a live GL context, so this must run from the render
     * loop, never at construction). Subsequent calls only update + draw.
     *
     * @param player Snapshot of the player's combat vitals to display.
     */
    void Draw(const CombatStats &player);

private:
    /// One vitals bar: its full-width transform (fraction 1), the fill drawable,
    /// and the inline numeric readout.
    struct Bar {
        std::shared_ptr<Util::GameObject> fill;
        std::shared_ptr<Util::Text> text;
        std::shared_ptr<Util::GameObject> textObj;
        Util::Transform full;   ///< Phase-1 absolute transform at fraction == 1.
        float texW = 64.0F;     ///< ui_12 native width (fill compensation basis).
        int lastValue = -1;     ///< last displayed cur; skip re-rasterizing if unchanged.
    };

    /// Build the HUD GameObjects from the layout JSON + baked PNGs (one-shot).
    void Build(const CombatStats &player);
    /// cur/max clamped to [0,1]; 0 when max <= 0.
    static float Fraction(int cur, int maxValue);

    Util::Renderer m_Renderer; ///< Screen-space HUD draw pass (own, not the world's).
    std::shared_ptr<Util::GameObject> m_Bg;
    Bar m_Hp;
    Bar m_Armor;
    Bar m_Energy;

    // --- A4: skill button group, bottom-right corner (ring + icon + cooldown mask) ---
    /// btn_skill background ring (ui_joy_0); skill icon (ui_joy_2); the cyan
    /// Vertical/Bottom Filled cooldown mask (ui_joy_3) driven by skillCdProgress.
    std::shared_ptr<Util::GameObject> m_SkillRing;
    std::shared_ptr<Util::GameObject> m_SkillIcon;
    std::shared_ptr<Util::GameObject> m_SkillMaskObj;
    std::shared_ptr<Util::FilledImage> m_SkillMask;

    bool m_Built = false; ///< Lazy-init attempted (success or not) -- try once.
    bool m_Ready = false; ///< Build() fully succeeded -> Draw renders; else no-op.
};
} // namespace Game

#endif /* GAME_HUD_HPP */
