#ifndef GAME_RGBULLETTRIGGER_HPP
#define GAME_RGBULLETTRIGGER_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RGBulletTrigger
 * @brief Faithful, engine-free port of the recoverable scalar/state math on the
 *        projectile HIT component (RGBulletTrigger) of Soul Knight 1.7.10.
 *
 * Per-content port. The decompiler recovers only a thin shell of this class: most
 * of the real work (the OnTriggerEnter2D damage dispatch, get_the_bullet, the
 * effect-trigger lists, PrefabPool spawning, vtable forwards) is owner-side and is
 * deliberately NOT modelled here. What is recoverable is the pierce-budget
 * (through_count) state, the can_through invariant, the SetInfo bool->budget
 * conversion, the GetDamageFactor non-ice return, the OnTriggerEnter2D gate head,
 * and the DestroyBullet branch predicates. Each is ported below with a decomp cite.
 *
 * WHAT THE DECOMP SHOWS (no reconstruction of unseen logic):
 *
 *  - get_through_count @ game_full.c:467542 -> `return *(this+0x18)`.
 *  - set_through_count @ game_full.c:467550 -> `*(this+0x18) = v;
 *    *(this+0x14) = (0 < v)` i.e. writes through_count AND derives can_through.
 *    A consumer (RGArrowThroughTrigger::SetPercentage @ 390729-390731) immediately
 *    re-reads get_through_count and re-stores `can_through = (0 < count)`,
 *    confirming the invariant can_through == (through_count > 0).
 *  - SetInfo(bool through) @ game_full.c:469750 -> `budget = through ? 0xff : 0`
 *    then forwards to the (int) overload via vtable (owner). 0xff is the
 *    "infinite pierce" sentinel.
 *  - GetDamageFactor @ game_full.c:469729 -> if (has_ice_buff (0x40) == 0) returns
 *    0x3f800000 (1.0f). When has_ice_buff != 0 the body tail-calls the
 *    Singleton<RGGameProcess> getter and TRUNCATES ("Subroutine does not return"):
 *    the ice-mode factor it would return is NOT in the decomp. The recreation .cs
 *    guess of 0.5f is unverifiable and is exposed only as a TODO[verify] constant,
 *    never used to fabricate a return.
 *  - OnTriggerEnter2D @ game_full.c:469051 -> the ONLY recovered statement is
 *    `*(this+0xc) = 0` (need_destory = false) at entry; the body then null-checks
 *    the collider and tail-calls Component::get_gameObject (truncation = owner).
 *    The entire per-tag damage dispatch in the recreation .cs is reconstructed
 *    (its own comment says so) and is owner-side; only the gate head is ported.
 *  - DestroyBullet @ game_full.c:468766 -> two branch predicates over the packed
 *    ushort at 0x30: `(low byte != 0)` == destory_parent spawns the impact VFX,
 *    `(0xff < ushort)` == stop_parent (0x31) runs a vtable forward. Both bodies are
 *    PrefabPool / get_the_bullet owner concerns; only the predicates are ported.
 *
 * Determinism: NO recovered body calls rg_random, so this unit makes ZERO RNG
 * draws (the crit roll in the recreation lives in the truncated owner-side
 * dispatch, not here). The RGRandom member is carried only for interface parity.
 *
 * @see recreation/Weapon/RGBulletTrigger.cs (field offsets / logic cross-check);
 *      FAITHFUL: RGBulletTrigger @ game_full.c:467507+.
 */
class RGBulletTrigger {
public:
    /// SetInfo's "infinite pierce" sentinel budget (game_full.c:469759, 0xff).
    static constexpr int kInfinitePierceBudget = 0xff;
    /// GetDamageFactor's recovered non-ice return (0x3f800000 == 1.0f).
    static constexpr float kNoBuffDamageFactor = 1.0F;
    /// AddEffectTrigger's BuffEffectTrigger critic_factor (0x3f800000 == 1.0f).
    static constexpr float kBuffCriticFactorAdd = 1.0F;
    /// RemoveEffectTrigger's BuffEffectTrigger critic_factor (0x40000000 == 2.0f).
    static constexpr float kBuffCriticFactorRemove = 2.0F;
    /// Ice-mode reduced factor the truncated ice branch MIGHT return. The decomp
    /// truncates before this value; the recreation guesses 0.5f. Unverifiable.
    // TODO[verify 0x5A9BBC]: ice-buff branch tail-calls Singleton<RGGameProcess>
    // and truncates; the returned factor is not recoverable from game_full.c.
    static constexpr float kIceBuffFactorUnverified = 0.5F;

    RGBulletTrigger() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // --- through_count: the pierce budget + can_through invariant -----------

    /**
     * @brief Read the pierce budget.
     * FAITHFUL: RGBulletTrigger__get_through_count @ game_full.c:467542
     * (`return *(this+0x18)`).
     */
    int GetThroughCount() const { return m_ThroughCount; }

    /**
     * @brief Set the pierce budget and derive the can_through flag.
     *
     * FAITHFUL: RGBulletTrigger__set_through_count @ game_full.c:467550
     * (`*(this+0x18) = v; *(this+0x14) = (0 < v)`). Writing the budget always
     * recomputes can_through = (budget > 0); the consumer at 390729-390731
     * re-asserts the same invariant.
     * @param value the new pierce budget (0xff == infinite).
     */
    void SetThroughCount(int value);

    /// Live can_through flag (0x14), always == (through_count > 0).
    bool CanThrough() const { return m_CanThrough; }

    // --- SetInfo: bool->budget conversion ----------------------------------

    /**
     * @brief Convert a can_through bool to its pierce budget.
     *
     * FAITHFUL: RGBulletTrigger__SetInfo(bool) @ game_full.c:469750
     * (`budget = (through != 0) ? 0xff : 0`). The decomp then forwards the budget
     * plus the other args to the (int) SetInfo overload through vtable slot 0xe4
     * (owner); this helper exposes only the recoverable scalar conversion.
     * @param canThrough the piercing toggle.
     * @return 0xff when @p canThrough, else 0.
     */
    static int ThroughBudgetFromBool(bool canThrough);

    /**
     * @brief Apply a SetInfo(bool) call: convert the flag and store the budget.
     *
     * FAITHFUL: composes ThroughBudgetFromBool (game_full.c:469758) with
     * set_through_count's state writes (game_full.c:467550). Mirrors the net
     * field effect of the two chained SetInfo overloads on through_count /
     * can_through; every other SetInfo field (damage, camp, repel, critical) is
     * stored by the owner-side (int) overload and is not modelled here.
     * @param canThrough the piercing toggle passed to SetInfo(bool).
     */
    void ApplySetInfoThrough(bool canThrough);

    // --- GetDamageFactor ---------------------------------------------------

    /**
     * @brief The recoverable part of GetDamageFactor.
     *
     * FAITHFUL: RGBulletTrigger__GetDamageFactor @ game_full.c:469729. When
     * has_ice_buff (0x40) is clear the body returns 0x3f800000 (1.0f). When it is
     * set the body tail-calls Singleton<RGGameProcess>::get_Inst and TRUNCATES;
     * the ice-mode factor is unrecoverable, so this method only models the
     * recovered non-ice return and reports whether the owner-side ice branch
     * would have been taken.
     * @param hasIceBuff the bullet's has_ice_buff flag (0x40).
     * @return 1.0f when @p hasIceBuff is false. When true, the real factor is
     *         decided owner-side; this returns 1.0f as the recovered baseline.
     */
    static float GetDamageFactor(bool hasIceBuff);

    /**
     * @brief Whether GetDamageFactor would enter the owner-side ice branch.
     * FAITHFUL: the `if (*(this+0x40) != 0)` guard @ game_full.c:469736.
     */
    static bool TakesIceBranch(bool hasIceBuff) { return hasIceBuff; }

    // --- OnTriggerEnter2D gate head ----------------------------------------

    /**
     * @brief The recovered head of OnTriggerEnter2D.
     *
     * FAITHFUL: RGBulletTrigger__OnTriggerEnter2D @ game_full.c:469058
     * (`*(this+0xc) = 0`). The decomp recovers ONLY this need_destory clear before
     * truncating at the collider get_gameObject tail-call; the full per-tag damage
     * dispatch is owner-side. Clears m_NeedDestroy and returns.
     */
    void OnTriggerEnterHead();

    /// Live need_destory flag (0xc), cleared by the trigger head.
    bool NeedDestroy() const { return m_NeedDestroy; }

    // --- DestroyBullet branch predicates -----------------------------------

    /**
     * @brief Whether DestroyBullet's impact-VFX branch fires.
     *
     * FAITHFUL: RGBulletTrigger__DestroyBullet @ game_full.c:468775
     * (`if ((*(ushort *)(this+0x30) & 0xff) != 0)` -> consult PrefabPool). The low
     * byte at 0x30 is destory_parent; when set, the owner spawns the pooled impact
     * VFX. Only the predicate is recovered; the PrefabPool spawn is owner.
     * @param destoryParent the bullet's destory_parent flag (0x30).
     */
    static bool DestroyBulletSpawnsVfx(bool destoryParent) { return destoryParent; }

    /**
     * @brief Whether DestroyBullet's parent-stop branch fires.
     *
     * FAITHFUL: RGBulletTrigger__DestroyBullet @ game_full.c:468784
     * (`if (0xff < *(ushort *)(this+0x30))`). The high byte at 0x31 is stop_parent;
     * when set, the owner fetches the_bullet and forwards through vtable slot 0x114.
     * Only the predicate is recovered; the forward is owner.
     * @param stopParent the bullet's stop_parent flag (0x31).
     */
    static bool DestroyBulletStopsParent(bool stopParent) { return stopParent; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};               ///< deterministic stream; never advanced here.
    int m_ThroughCount = 0;         ///< pierce budget (0x18); 0xff == infinite.
    bool m_CanThrough = false;      ///< can_through (0x14); == (m_ThroughCount > 0).
    bool m_NeedDestroy = false;     ///< need_destory (0xc); cleared at trigger entry.
};

} // namespace Game

#endif /* GAME_RGBULLETTRIGGER_HPP */
