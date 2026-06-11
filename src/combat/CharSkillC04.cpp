#include "combat/CharSkillC04.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC04::CharSkillC04(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash / C01.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL (gate only): C04Controller__RoleSkill @ game_full.c:156371 --
// awake (this+0xC) && role_attribute (this+0x44) != null && skill_ready &&
// !in_skill (this+0x55). The awake check is the caller's responsibility; the gate
// this unit owns is skill_ready && !in_skill, exactly the nested-if at
// 156381/156386/156390. NO RGRandom draw on this path.
//
// RECONSTRUCTED window-open (flagged; see fabrication_flags item (c)): the recovered
// RoleSkill HEAD writes NO flags -- past the gate it is purely a get_transform
// tail-call at 156393 (owner: hero-specific sword spawn). in_skill = true and the
// sword-cut window open (in_skill_effect = true, has_cut = false) are NOT in the
// recovered RoleSkill body; they live in the owner-side transform tail / Invoke
// that 156393 dispatches to. We model them here so TryActivateSkill / RoleAtk are
// unit-testable as one decision, in the same flagged style as the reused cooldown
// engine -- NOT as plain faithful RoleSkill behavior.
bool CharSkillC04::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false; // already in skill, or still on cooldown -> no-op
    }
    // FAITHFUL: the gate passed (skill_ready && !in_skill). The cooldown spend is
    // deferred to EndSkill (RoleSkillEnd -> ReSetSkillReload) -- no recovered C04
    // body spends it. The three writes below are the RECONSTRUCTED window-open.
    m_InSkill = true;       // RECONSTRUCTED: not written by the RoleSkill head.
    m_InSkillEffect = true; // RECONSTRUCTED: window open lives in the owner tail.
    m_HasCut = false;       // RECONSTRUCTED: clean latch for the new window.
    return true;
}

// Models RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload (NOT in
// any recovered C04 body; see fabrication_flags): leave the skill state and
// restart the cooldown (this_skill_time = 0). The sword-effect window is left
// alone here -- it closes via KillSomeOne / EndSkillEffect, not RoleSkillEnd.
void CharSkillC04::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;
    m_ThisSkillTime = 0.0F; // ReSetSkillReload(): restart the cooldown
}

// FAITHFUL: C04Controller__RoleAtk @ game_full.c:156423. Branch order preserved:
//   awake gate -> in_item && press && !in_skill -> TriggerItem early-out
//   -> if in_skill_effect == 0 (this+0x94): normal hand.SetAttack(value)
//   -> else if has_cut != 0 (this+0x95): hand.SetAttack(value), return
//   -> else if press: AtkCut(atk + (skill_strengthen ? 7 : 3)) and latch has_cut.
// The awake outer gate (param_1[3], this+0xC) is the caller's responsibility.
// NO RGRandom draw on this path.
CharSkillC04::AtkDecision CharSkillC04::RoleAtk(bool pressDown, bool standingOnItem,
                                               int atk, bool skillStrengthen) {
    AtkDecision d;

    // 156434-156438: in_item && press && !in_skill -> trigger the pickup, return.
    // (Note the !in_skill guard here -- the item-trigger is blocked DURING the
    // ultimate, unlike C01 which stops the 2nd hand and triggers anyway.)
    if (standingOnItem && pressDown && !m_InSkill) {
        d.triggerItem = true;
        return d; // RGController.TriggerItem(); return;
    }

    // 156440: branch on the sword-cut effect window (in_skill_effect, this+0x94).
    if (!m_InSkillEffect) {
        // 156441-156467: normal hand path. A press + aim-virtual predicate gates a
        // non-melee aim nudge (get_position tail @ 156458, owner-only, no pure
        // logic / RNG), then hand.SetAttack(value) fires/stops the gun.
        d.setHandAttack = true;
        d.handAttackValue = pressDown;
        return d;
    }

    // 156469-156477: effect window is up and a cut already fired this window
    // (has_cut != 0, the 0xff < ushort high-byte test) -> just pass the press to
    // the hand and return; do not cut again.
    if (m_HasCut) {
        d.setHandAttack = true;
        d.handAttackValue = pressDown;
        return d;
    }

    // 156478-156491: effect window up, not yet cut. A press performs ONE big cut:
    //   dmg = role_attribute.atk (attr+0x40) + (skill_strengthen ? 7 : 3);
    //   AtkCut(dmg);   // owner: RGMusicManager.PlayEffect + Instantiate RGSword
    //   has_cut = 1;   // latch so a held button cuts only once per window
    // A release (param_2 != 1) while uncut does nothing (no else in the decomp).
    if (pressDown) {
        d.doCut = true;
        d.cutDamage =
            atk + (skillStrengthen ? kCutBonusStrengthened : kCutBonusNormal);
        m_HasCut = true;
    }
    return d;
}

// FAITHFUL: C04Controller__KillSomeOne @ game_full.c:156499. Only the in-window
// kills count: combo (this+0x98) advances, the 5th wraps to 0 with NO refresh,
// every other kill refreshes the cooldown to ready (ReflashSkillCd: this_skill_time
// = skill_cd). Either way the effect window then ends (CancelInvoke +
// EndSkillEffect, which clears in_skill_effect). The base RGController.KillSomeOne
// (always called) is the owner's concern. NO RGRandom draw.
bool CharSkillC04::KillSomeOne() {
    if (!m_InSkillEffect) {
        return false; // outside the window only the base KillSomeOne runs (owner)
    }

    // 156507-156521: combo++ ; if (combo == 5) combo = 0; else ReflashSkillCd().
    m_Combo += 1;
    if (m_Combo == kComboWrap) {
        m_Combo = 0; // chain wraps; no cooldown refresh on the 5th kill
    } else {
        // ReflashSkillCd(): this_skill_time = skill_cd -> ready immediately again.
        // (UpdateShadowLock @ 156520 is owner-side; the UI reflash is too.)
        m_ThisSkillTime = m_SkillCd;
    }

    // 156522-156523: CancelInvoke("...") + EndSkillEffect(). EndSkillEffect @156401
    // is owner-side (energy-1 at attr+0x14, Renderer.set_enabled(false) at this+0x90)
    // EXCEPT its sole brain write -- the single flag clear we model so the next
    // window's gate starts clean:
    //   *(undefined1 *)(param_1 + 0x94) = 0;   // in_skill_effect = 0 (ONLY byte 0x94)
    // FAITHFUL: EndSkillEffect writes ONLY 0x94. It does NOT clear has_cut (0x95);
    // no recovered C04 body writes has_cut = 0 here. (has_cut is re-cleared on the
    // next TryActivateSkill -- see fabrication_flags item (c) -- so it is left as-is
    // here rather than silently cleared in this body.)
    m_InSkillEffect = false;
    return true;
}

// RECONSTRUCTED (flagged): C04Controller has NO recovered Update body, so this is
// the established CharSkillC01 / RoleAttributePlayer.SkillReload cooldown model
// reused for testability: this_skill_time counts UP by dt, clamped to skill_cd.
// C04 has no recovered in_skill_time countdown / auto-end, so Tick does NOT end
// the skill or the effect window. NO RGRandom draw.
void CharSkillC04::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }
}

float CharSkillC04::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
