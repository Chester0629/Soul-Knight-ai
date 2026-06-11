#ifndef GAME_BOSS_AI04_HPP
#define GAME_BOSS_AI04_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI04
 * @brief Faithful phase/cadence logic for the second boss (RGEController subclass).
 *
 * Per-content port (report #5): a boss-specific logic layer over the
 * deterministic RGRandom stream. Models only the pure, unit-testable brain;
 * sprite swaps, animator triggers, bullet spawns and Invoke() chains are left
 * to the owning entity. Modelled here:
 *   - the attack-index state machine: atk_index cycles 1..5 (Atk01..Atk05),
 *     reset to 0 by StopAttack (BossAI04.StopAttack);
 *   - the single angry-phase transition at hp/max_hp < 0.5 (BossAngry), once;
 *   - the deterministic attack selection roll (ShootReflection's
 *     rg_random.Range(0, 100));
 *   - the angry-gated cadence change: Atk02's re-fire delay is Range(2,4)s,
 *     +1.0s while angry (BossAI04.StartAtk02).
 *
 * @see recreation BossAI04.cs; FAITHFUL: BossAI04 @ game_full.c:438840-439900
 *      (boss04: max_hp=600, ai_level=2, shoot_cd=2).
 */
class BossAI04 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// Number of distinct attacks: Atk01..Atk05 (atk_index in [1, kAttackCount]).
    static constexpr int kAttackCount = 5;
    /// Idle / no-attack sentinel for atk_index (set by StopAttack).
    static constexpr int kNoAttack = 0;
    /// Range roll ceiling used by ShootReflection (Range(0, 100), max exclusive).
    static constexpr int kRollCeiling = 100;
    /// Atk02 re-fire delay bounds (Range(2f, 4f), max inclusive).
    static constexpr float kAtk02DelayMin = 2.0F;
    static constexpr float kAtk02DelayMax = 4.0F;
    /// Extra Atk02 windup applied while angry (BossAI04.StartAtk02: delay += 1f).
    static constexpr float kAngryAtk02DelayBonus = 1.0F;

    BossAI04() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Apply a hit's resulting HP; enters the angry phase once at < 50%.
     * FAITHFUL: BossAI04.GetHurt -> BossAngry (field 0xf9, hp/max_hp < 0.5).
     */
    void OnHurt(int hpAfter, int maxHp);

    bool Angry() const { return m_Angry; }

    /// Current attack-state index (0 = idle, 1..5 = Atk01..Atk05).
    int AtkIndex() const { return m_AtkIndex; }

    /**
     * @brief Roll-and-dispatch one attack: the deterministic attack selection.
     *
     * FAITHFUL to ShootReflection's single `rg_random.Range(0, 100)` draw. The
     * original's roll->attack jumptable could not be recovered from the decomp
     * (indirect jump at 0x542b64), so the 5 equal buckets here are a
     * reconstruction (see manual_flags). The single Range(0,100) draw keeps the
     * stream in lockstep with the original regardless of bucketing. Sets and
     * returns atk_index in [1, kAttackCount]; the owning entity calls the
     * matching StartAtkNN to drive the animator/bullets.
     * @return chosen attack index in [1, kAttackCount].
     */
    int ChooseAttack();

    /**
     * @brief Tear down the active attack (BossAI04.StopAttack): atk_index -> 0.
     *
     * The original switch only clears state for cases 2..5 (Atk01 self-clears at
     * its bullet release); we faithfully reset atk_index to the idle sentinel
     * for any active attack so the next ChooseAttack starts clean.
     */
    void StopAttack();

    /**
     * @brief Compute the Atk02 re-fire delay (seconds) for the burst chain.
     *
     * FAITHFUL: BossAI04.StartAtk02 -> Range(2f, 4f); +1.0s while angry. Draws
     * one float from the stream (keeps cadence deterministic + lockstep).
     */
    float Atk02RefireDelay();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Angry = false;
    int m_AtkIndex = kNoAttack;
};

} // namespace Game

#endif /* GAME_BOSS_AI04_HPP */
