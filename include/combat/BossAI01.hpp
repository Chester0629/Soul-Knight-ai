#ifndef GAME_BOSS_AI01_HPP
#define GAME_BOSS_AI01_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI01
 * @brief Faithful phase logic for the first boss (RGEController subclass).
 *
 * Proof-of-pattern for the per-content ports (report #5): a boss-specific layer
 * over the deterministic RGEController/RGRandom base. Models the parts that are
 * pure logic + unit-testable:
 *   - the single angry-phase transition at hp/max_hp < 0.5 (BossAngry): halve
 *     shoot_cd, bump anim speed to 1.2x -- once only;
 *   - the deterministic attack selection (ShootReflection's Range(0,100) roll);
 *   - the random wander direction (RunReflection's Range(-1,1) x2, normalized).
 *
 * The animation-event bullet spawns (InAtk01..04) are scene/bullet wiring and
 * are left to the owning entity; this is the decision/cadence brain.
 *
 * @see recreation BossAI01.cs; FAITHFUL: BossAI01 @ game_full.c:436541-437280
 *      (boss01: max_hp=600, ai_level=1, shoot_cd=2).
 */
class BossAI01 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// shoot_cd multiplier on entering angry (attacks ~2x faster).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// Animator speed on entering angry.
    static constexpr float kAngryAnimSpeed = 1.2F;
    /// Number of attack animations (InAtk01..InAtk04).
    static constexpr int kAttackCount = 4;

    explicit BossAI01(float baseShootCd);

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Apply a hit's resulting HP; enters the angry phase once at < 50%.
     * FAITHFUL: BossAI01.GetHurt -> BossAngry.
     */
    void OnHurt(int hpAfter, int maxHp);

    bool Angry() const { return m_Angry; }
    float ShootCd() const { return m_ShootCd; }
    float AnimSpeed() const { return m_AnimSpeed; }

    /**
     * @brief Pick which of the 4 attacks to trigger.
     *
     * FAITHFUL to ShootReflection's `rg_random.Range(0, 100)` roll. The roll->attack
     * bucketing is BLOCKED, not merely unverified: ShootReflection tail-calls the no-return
     * stub FUN_010b7dcc immediately after Range(0,100) (game_full.c:436790-436793), so Ghidra
     * never linearized the dispatch -- the roll value is not even stored. The 4 equal 25-wide
     * buckets are an unverifiable reconstruction (recovering the real thresholds needs a raw
     * disassembly past FUN_010b7dcc or a runtime trace). The single Range(0,100) draw -- the
     * one decomp-confirmed fact -- keeps the stream in lockstep.
     * @return attack index in [0, kAttackCount).
     */
    int ChooseAttack();

    /// Random wander direction -- the NO-TARGET branch of RunReflection (Range(-1,1) x2,
    /// normalized). The sim always supplies a target, so @ref ChaseMoveDecision runs instead;
    /// this is kept for faithfulness and is exercised directly by the brain tests.
    glm::vec2 WanderDirection();

    /**
     * @brief The WITH-TARGET move decision (RunReflection, game_named.c:122666-122723).
     *
     * FAITHFUL: with a player target the boss defaults to @p chaseDir, then ONE per-cycle
     * switch overrides it on a fresh roll:
     *   - roll in [0,6): keep chase, BUT if @p dist < a Range(5,10) threshold, RETREAT
     *     (negate chase) -- the boss backs off when the player gets too close;
     *   - roll in [6,8): strafe by mirroring X (-chase.x, chase.y);
     *   - roll in [8,10): strafe by mirroring Y (chase.x, -chase.y).
     * Draw ORDER is load-bearing for RNG parity: the Range(5,10) threshold is drawn BEFORE
     * the Range(0,10) selector (both are int draws, so this consumes the same 2 stream steps
     * as @ref WanderDirection -- the attack roll downstream is unaffected).
     * @param chaseDir unit direction toward the player (normalize(player - own)).
     * @param dist     world-space distance from the boss to the player.
     * @return the chosen move direction (unit, except a zero @p chaseDir passes through).
     */
    glm::vec2 ChaseMoveDecision(glm::vec2 chaseDir, float dist);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Angry = false;
    float m_ShootCd;
    float m_AnimSpeed = 1.0F;
};

} // namespace Game

#endif /* GAME_BOSS_AI01_HPP */
