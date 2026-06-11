#include "combat/CharSkillC09.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC09::CharSkillC09(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      // Start ready: a freshly SetUpChar archer has its first draw available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash/C01.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C09Controller__RoleSkill @ game_full.c:157300 -- a TOGGLE.
//   if (!awake) return;                          // this+0xC (caller's job)
//   if (in_skill) { RoleSkillEnd(); return; }    // slot 0x17c @ 157315: release
//   if (skill_ready) { CreateArrow(); spawn; }   // 157322-157326: draw
// The awake gate is the caller's responsibility (this brain has no awake field).
// The arrow spawn (get_transform tail @ 157326) and the in_skill latch are owner /
// flagged base-class reuse ([CharSkillC09-LATCH]); CreateArrow @ 157324 is animator
// bools only (owner). NO RGRandom draw.
CharSkillC09::SkillDecision CharSkillC09::RoleSkill() {
    SkillDecision d;

    // 157312-157316: already drawn -> the press RELEASES the bow (RoleSkillEnd).
    if (m_InSkill) {
        Release(); // models slot 0x17c -> RoleSkillEnd (+ base ReSetSkillReload spend)
        d.releasedBow = true;
        return d;
    }

    // 157322-157326: not drawn -> draw iff charged. get_skill_ready == 1.
    if (!SkillReady()) {
        return d; // on cooldown -> the press is swallowed (no draw, no spend)
    }
    // Enter the drawn state. The actual arrow spawn (get_transform tail) is the
    // owner's job; the in_skill latch is the flagged base-class reuse
    // ([CharSkillC09-LATCH]) -- C09's recovered head truncates before setting it.
    m_InSkill = true;
    d.drewBow = true;
    return d;
}

// Models RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload
// ([CharSkillC09-SPEND]): C09's own RoleSkillEnd @ 157395 only flips an animator
// bool; the in_skill clear + cooldown restart are the base chain. Leave the drawn
// state and restart the cooldown (this_skill_time = 0). No-op if not drawn.
void CharSkillC09::Release() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;
    m_ThisSkillTime = 0.0F; // ReSetSkillReload(): restart the cooldown
}

// FAITHFUL: C09Controller__RoleAtk @ game_full.c:157566.
// Branch order preserved:
//   awake gate (caller's job, 157576) ->
//   in_item && press (157580): in_skill ? fall-through : TriggerItem+return (157581) ->
//   else !in_skill -> goto normalHand (157586) ->
//   bow-block (reached ONLY while in_skill): skill_ready (157591) && the_bullet!=null
//     (157598) -> ArrowShoot+return (157600) ->
//   normalHand (157604): press && weaponHasFrontGun(slot 0xe4) && !IsMelee -> truncated
//     get_position aim-nudge (157616, owner) -> hand.SetAttack(value) (157623).
// NO RGRandom draw on this path.
CharSkillC09::AtkDecision CharSkillC09::RoleAtk(bool pressDown, bool standingOnItem,
                                               bool bulletNocked) const {
    AtkDecision d;

    // 157579-157586: in_item && press. While NOT drawn, the press uses the pickup
    // and returns; while drawn, it falls through to the bow-block (it does NOT use
    // the item). When (in_item && press) is false and NOT drawn, the bow-block is
    // skipped entirely (goto normalHand).
    const bool itemPress = standingOnItem && pressDown;
    if (itemPress && !m_InSkill) {
        d.triggerItem = true;
        return d; // RGController.TriggerItem(); return;
    }

    // 157587-157603: the bow-shot block is reachable only while in_skill (both the
    // itemPress-and-in_skill fall-through and the else-branch require in_skill).
    if (m_InSkill) {
        // 157591/157598: charged AND an arrow is nocked -> loose it and return.
        if (SkillReady() && bulletNocked) {
            d.arrowShoot = true;
            return d; // C09Controller__ArrowShoot(); return;
        }
    }

    // 157604-157620: a press-only, non-melee aim nudge (get_position) is truncated
    // at the tail-call and carries no recoverable pure logic / RNG, so it is
    // intentionally not modeled.

    // 157623: hand.SetAttack(value) on the primary hand (fires/stops the weapon).
    d.setHandAttack = true;
    d.handAttackValue = pressDown;
    return d;
}

// FAITHFUL: C09Controller__Update @ game_full.c:157193.
//   awake gate (157213) -> RGController__AttributeUpdate (157214) -> SkillReload:
//   this_skill_time counts up, clamped to skill_cd, EVERY awake frame. The in_skill
//   aim branch (157215-157265) is an aim-angle computation (switch_axis_mode /
//   Vector2.Angle / set_localEulerAngles, all owner / truncated get_transform) with
//   NO timer; SeachUpdate (157276) is owner. UNLIKE C01/C10, there is NO
//   in_skill_time countdown and NO auto-end here, so Tick() advances ONLY the
//   cooldown and never ends the drawn bow on a timer. NO RGRandom draw.
void CharSkillC09::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // AttributeUpdate() -> SkillReload(dt): this_skill_time counts up, clamped to
    // skill_cd. Unconditional: it advances even while the bow is drawn (the in_skill
    // branch sits AFTER AttributeUpdate in the decomp and contains no cooldown work).
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }
}

float CharSkillC09::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
