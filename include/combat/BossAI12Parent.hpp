#ifndef GAME_BOSS_AI12_PARENT_HPP
#define GAME_BOSS_AI12_PARENT_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI12Parent
 * @brief Faithful coordinator brain for the two-half boss #12 (BossAI12Parent).
 *
 * Per-content port. BossAI12Parent is a plain MonoBehaviour (NOT an
 * RGEController), so unlike BossAI04 it owns NO rg_random stream and performs
 * ZERO RNG draws: every random draw for this boss lives in the two child
 * RGEController halves (class BossAI12 - Scout/RunReflection/ShootReflection).
 * The parent's only job is to aggregate the two halves' vitals, gate the
 * "boss is truly dead" transition, and trigger owner-side prefab spawns.
 *
 * Decomp bodies of the parent class (game_full.c):
 *   - BossAI12Parent__UpDateBossHp        @ 957447  (PURE LOGIC, modelled)
 *   - BossAI12Parent__BossDead            @ 957553  (PURE LOGIC gate, modelled)
 *   - BossAI12Parent__Start               @ 957823  (OWNER: prefab instantiate)
 *   - BossAI12Parent__CreateTransferGate  @ 957855  (OWNER: hide bar + prefab)
 *
 * Modelled here:
 *   - UpDateBossHp: total_hp = ai1.hp + ai2.hp; total_max = ai1.max_hp +
 *     ai2.max_hp; both forwarded to BossInfo.UpDateBossHp(current, max).
 *   - The HP-bar fill width derived by BossInfo__UpDateBossHp @ 956318:
 *     sizeDelta.x = (current * 400) / max  (kHpBarFullWidth = 400).
 *   - BossDead: the both-halves-dead gate - the parent advances to the real
 *     death path ONLY when both child halves have their dead latch (0x38) set;
 *     if either half is still alive, BossDead returns early (no transition).
 *
 * Field offsets (decoded against RGEController.cs / RoleAttribute.cs):
 *   BossAI12Parent: boss_info @0x0c, boss_ai_1 @0x18, boss_ai_2 @0x1c.
 *   RGEController (child half): role_attribute @0x70, dead @0x38.
 *   RoleAttribute: max_hp @0x18, hp @0x1c.
 *
 * @see recreation RGEController.cs (offset table); FAITHFUL: BossAI12Parent @
 *      game_full.c:957447-957884.
 */
class BossAI12Parent {
public:
    /// HP-bar full pixel width: BossInfo__UpDateBossHp sizeDelta.x =
    /// (current * 400) / max  (constant 400.0 @ game_full.c:956337).
    static constexpr float kHpBarFullWidth = 400.0F;

    BossAI12Parent() = default;

    /**
     * @brief Aggregated current HP of the two boss halves (ai1.hp + ai2.hp).
     *
     * FAITHFUL: BossAI12Parent__UpDateBossHp arg2 =
     *   *(ai2.role_attribute + 0x1c) + *(ai1.role_attribute + 0x1c).
     */
    static int TotalHp(int ai1Hp, int ai2Hp) { return ai1Hp + ai2Hp; }

    /**
     * @brief Aggregated max HP of the two boss halves (ai1.max_hp + ai2.max_hp).
     *
     * FAITHFUL: BossAI12Parent__UpDateBossHp arg3 =
     *   *(ai2.role_attribute + 0x18) + *(ai1.role_attribute + 0x18).
     */
    static int TotalMaxHp(int ai1MaxHp, int ai2MaxHp) {
        return ai1MaxHp + ai2MaxHp;
    }

    /**
     * @brief Update the shared HP bar from both halves; returns the bar fill
     *        width in pixels (the value BossInfo writes to sizeDelta.x).
     *
     * FAITHFUL: BossAI12Parent__UpDateBossHp -> BossInfo__UpDateBossHp
     * (sizeDelta.x = (current_total * 400) / max_total). No RNG draw. Caches the
     * aggregated totals so HpBarWidth()/TotalHp()/TotalMaxHp() reflect the last
     * update; the owning entity drives the actual RectTransform resize.
     * @return bar fill width in pixels (0 when max total HP is 0).
     */
    float UpDateBossHp(int ai1Hp, int ai1MaxHp, int ai2Hp, int ai2MaxHp);

    /**
     * @brief The "boss truly dead" gate: both halves must have died.
     *
     * FAITHFUL: BossAI12Parent__BossDead - returns early unless BOTH
     * boss_ai_1.dead (0x38) AND boss_ai_2.dead (0x38) are set; only then does
     * the original proceed to the (owner-side) PlayerSaveData tail. No field
     * write, no RNG draw - this is a pure read-only gate.
     * @return true when both halves are dead (the death transition fires).
     */
    static bool BossDead(bool ai1Dead, bool ai2Dead) {
        return ai1Dead && ai2Dead;
    }

    /// Last aggregated current HP (from the most recent UpDateBossHp call).
    int LastTotalHp() const { return m_LastTotalHp; }
    /// Last aggregated max HP (from the most recent UpDateBossHp call).
    int LastTotalMaxHp() const { return m_LastTotalMaxHp; }
    /// Last computed HP-bar fill width (pixels).
    float LastHpBarWidth() const { return m_LastHpBarWidth; }

private:
    int m_LastTotalHp = 0;
    int m_LastTotalMaxHp = 0;
    float m_LastHpBarWidth = 0.0F;
};

} // namespace Game

#endif /* GAME_BOSS_AI12_PARENT_HPP */
