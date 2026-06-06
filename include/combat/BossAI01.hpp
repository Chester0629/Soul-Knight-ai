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
     * FAITHFUL to ShootReflection's `rg_random.Range(0, 100)` roll; the original's
     * roll->attack bucketing is truncated in the decomp, so the 4 equal buckets
     * here are a reconstruction (TODO[verify] exact thresholds). The single
     * Range(0,100) draw keeps the stream in lockstep.
     * @return attack index in [0, kAttackCount).
     */
    int ChooseAttack();

    /// Random wander direction (RunReflection: Range(-1,1) x2, normalized).
    glm::vec2 WanderDirection();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Angry = false;
    float m_ShootCd;
    float m_AnimSpeed = 1.0F;
};

} // namespace Game

#endif /* GAME_BOSS_AI01_HPP */
