#include "combat/CharSkillC06.hpp"

#include <algorithm>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
} // namespace

CharSkillC06::CharSkillC06(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      // Start ready: a freshly SetUpChar hero has its first ultimate available
      // (skill_ready == skill_cd <= this_skill_time). Matches PlayerDash / C01.
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// FAITHFUL: C06Controller__RoleSkill @ game_full.c:156632 gate (A, ACTIVATE) --
// awake && role_attribute.skill_ready (0x44) && !in_skill (0x55). (The awake
// check at 156640/this+0xC is the caller's responsibility; the gate this unit
// owns is skill_ready && !in_skill, exactly the nested-if at 156648/156656.)
bool CharSkillC06::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false; // already deployed, or still on cooldown -> no-op
    }
    // Enter the skill state (in_skill = true) and mark a battery deployed. The
    // hero-specific effect is the get_transform tail-call at 156658 (owner): the
    // real deploy helper (FUN_00266544) sets in_skill (0x55), Instantiates the
    // battery template (0x90) into the_battery (0x94), and arms the delayed Invoke
    // to TurnSkillCancelable. The cooldown spend is deferred to EndSkill
    // (RoleSkillEnd -> ReSetSkillReload), not done here -- the C06 body itself does
    // not spend it. canCancelSkill starts false: the recall is locked until
    // TurnSkillCancelable fires.
    m_InSkill = true;
    m_BatteryDeployed = true;
    m_CanCancelSkill = false;
    return true;
}

// FAITHFUL: C06Controller__RoleSkill @ game_full.c:156662 gate (B, EARLY RECALL):
//   if (in_skill)                                  // this+0x55
//     if (the_battery != null  (0x94 op_Inequality) && canCancelSkill (0x98))
//       the_battery.Dead();                         // RGBatteryController.Dead(0x94)
// The Dead() call is an animator-trigger tail (owner). Killing the battery ends
// the active window, so we route through the same spend path EndSkill models.
bool CharSkillC06::TryCancelSkill() {
    if (!m_InSkill || !m_BatteryDeployed || !m_CanCancelSkill) {
        return false; // gate failed: not deployed, no battery, or recall still locked
    }
    // the_battery.Dead() (owner). The recall ends the skill: leave the state and
    // spend the charge via the RoleSkillEnd path.
    EndSkill();
    return true;
}

// FAITHFUL (owner-driven): C06Controller.TurnSkillCancelable -- the delayed-Invoke
// target armed on deploy. Unlocks the early recall (canCancelSkill = true). The
// literal Invoke delay is not in the recovered head (FUN_00266544's Invoke args
// are truncated), so the owner fires this when the timer elapses.
void CharSkillC06::TurnSkillCancelable() {
    if (!m_InSkill) {
        return; // no active skill -> nothing to unlock
    }
    m_CanCancelSkill = true;
}

// Models RGController.RoleSkillEnd -> RoleAttributePlayer.ReSetSkillReload: leave
// the skill state, clear the deployed battery + recall gate, and restart the
// cooldown (this_skill_time = 0). NOT present in the recovered C06 body (it lives
// in the base chain) -- see fabrication note.
void CharSkillC06::EndSkill() {
    if (!m_InSkill) {
        return;
    }
    m_InSkill = false;
    m_BatteryDeployed = false; // the_battery cleared
    m_CanCancelSkill = false;  // recall gate reset
    m_ThisSkillTime = 0.0F;    // ReSetSkillReload(): restart the cooldown
}

// FAITHFUL: C06Controller__Update @ game_full.c:156619 (fully recovered, pure):
//   if (awake) { AttributeUpdate(); SeachUpdate(); }
// AttributeUpdate -> SkillReload counts the cooldown UP every awake frame.
// SeachUpdate (aim re-acquire) is an owner concern. NOTE: unlike C01, there is NO
// in_skill_time countdown and NO auto-end in C06's Update -- the deployed-battery
// skill does not self-terminate on a timer (it ends via recall or RoleSkillEnd).
// NO RGRandom draw.
void CharSkillC06::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    const float dt = dtMs / kMsPerSecond;

    // AttributeUpdate() -> SkillReload(dt): this_skill_time counts up, clamped to
    // skill_cd. Runs every awake frame (the awake gate at this+0xC is the caller's
    // responsibility). It is NOT frozen while in_skill -- C06's Update has no
    // in_skill branch at all, so the cooldown simply advances unconditionally here.
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dt, m_SkillCd);
    }
}

float CharSkillC06::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
