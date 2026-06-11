#ifndef GAME_BOSS_AI12_HPP
#define GAME_BOSS_AI12_HPP

#include "data/RGRandom.hpp"

#include <glm/glm.hpp>

namespace Game {

/**
 * @class BossAI12
 * @brief Faithful pure-logic brain for Soul Knight 1.7.10 boss "BossAI12"
 *        (an RGEController subclass; the twin-turret "Magic Lamp / Genie"-style
 *        boss whose HP is aggregated by BossAI12Parent across two body halves).
 *
 * Per-content port: a deterministic logic layer over the per-instance RGRandom
 * stream. Only the pure, unit-testable brain is modelled here; Animator
 * triggers, RGWeapon/RGBullet/RGELaser Instantiate, RGMusicManager.PlayEffect,
 * Rigidbody2D velocity writes and Transform reads are left to the owning entity
 * (one-line owner comments mark them in the .cpp).
 *
 * Modelled state + the EXACT field writes the decomp performs:
 *   - angry (field 0xcc, bool): set once by BossAngry, which also halves
 *     shoot_cd (0x3c *= 0.5). Triggered from GetHurt when hp/max_hp < 0.5 and
 *     not already angry (BossAI12.GetHurt -> BossAI12.BossAngry).
 *   - dizzy (field 0xa1, bool): set by Dizzy (only when not dead).
 *   - target_obj cleared (field 0x7c = 0) by Scout when neither dizzy nor dead.
 *   - the RunReflection wander roll: two Range(-1f, 1f) float draws normalised
 *     into a move direction (BossAI12.RunReflection).
 *   - the ShootReflection attack-selection roll: a single Range(0, 100) int draw
 *     that is GATED by can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1)
 *     (BossAI12.ShootReflection). Gated-out paths take NO draw (stream lockstep).
 *   - InAtk01 bullet count: angry ? 2 : 1 (BossAI12.InAtk01; reads angry 0xcc).
 *   - InAtk03 angle step: 180 / (angry ? 6 : 5) degrees (BossAI12.InAtk03,
 *     __udivsi3(0xb4, divisor); reads angry 0xcc).
 *   - EndAtk04 cadence relax: shoot_cd (0x14 on the attack-param struct) -= 1.0
 *     (BossAI12.EndAtk04).
 *
 * The original ShootReflection roll -> InAtkNN dispatch is an indirect/vtable
 * tail call (decomp tail-calls *(*param_1 + 0x10c)); the roll->attack jumptable
 * is not recoverable, so ChooseAttack returns the raw [0,100) roll and the
 * caller maps it. The single draw keeps the stream in lockstep regardless.
 *
 * @see recreation Enemy/RGEController.cs (base field offsets);
 *      metadata BossAI12.cs (field names: angry, atk_count, bullet01..05).
 *      FAITHFUL: BossAI12 @ game_full.c:957145-957830.
 */
class BossAI12 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (GetHurt gate).
    static constexpr float kAngryHpFraction = 0.5F;
    /// shoot_cd (field 0x3c) is multiplied by this on entering angry (BossAngry).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// Animator speed set on entering angry (anim.set_speed(1.2f) = 0x3f99999a).
    static constexpr float kAngryAnimSpeed = 1.2F;

    /// RunReflection wander draws: Range(-1f, 1f) per axis (max inclusive).
    static constexpr float kWanderAxisMin = -1.0F;
    static constexpr float kWanderAxisMax = 1.0F;

    /// ShootReflection attack-selection roll ceiling (Range(0, 100), max excl).
    static constexpr int kRollCeiling = 100;

    /// InAtk01 emits this many bullets when calm vs. angry.
    static constexpr int kInAtk01BulletsCalm = 1;
    static constexpr int kInAtk01BulletsAngry = 2;

    /// InAtk03 spreads bullets over 180 degrees: step = 180 / divisor.
    static constexpr int kInAtk03Arc = 180;
    static constexpr int kInAtk03DivisorCalm = 5;
    static constexpr int kInAtk03DivisorAngry = 6;

    /// EndAtk04 relaxes the attack-param shoot_cd by this many seconds.
    static constexpr float kEndAtk04ShootCdDelta = -1.0F;

    BossAI12() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    bool Angry() const { return m_Angry; }
    bool Dizzy() const { return m_Dizzy; }
    bool Dead() const { return m_Dead; }
    bool HasTarget() const { return m_HasTarget; }
    float ShootCd() const { return m_ShootCd; }

    /// Seed the cadence scalar that BossAngry halves (RGEController shoot_cd 0x3c).
    void SetShootCd(float shootCd) { m_ShootCd = shootCd; }
    /// Drive the dead latch (RGEController dead 0x38) used by Scout/Dizzy gates.
    void SetDead(bool dead) { m_Dead = dead; }

    /**
     * @brief FixedUpdate-time scout tick: clear the chase target when idle.
     *
     * FAITHFUL: BossAI12.Scout @ game_full.c:957229. Gated by !dizzy(0xa1) &&
     * !dead(0x38); only then is target_obj(0x7c) zeroed (the rest is a transform
     * read, owner-side). No RNG draw.
     */
    void Scout();

    /**
     * @brief RunReflection wander: two Range(-1f, 1f) float draws normalised.
     *
     * FAITHFUL: BossAI12.RunReflection @ game_full.c:957254/957259. Draws x then
     * y from the stream (order matters), normalises, and the owner writes it to
     * move_direction + sets anim "walk". Returns the unit direction (zero vector
     * for the degenerate (0,0) draw).
     */
    glm::vec2 WanderDirection();

    /**
     * @brief ShootReflection attack-selection roll, fully gated.
     *
     * FAITHFUL: BossAI12.ShootReflection @ game_full.c:957312. The single
     * Range(0, 100) draw happens ONLY when can_shoot(0x40) is set AND dead(0x38)
     * is clear AND dizzy(0xa1) is clear. Gated-out callers take NO draw (stream
     * lockstep). Pass the live gate bits.
     *
     * @param canShoot RGEController can_shoot flag (field 0x40).
     * @param dead     RGEController dead latch (field 0x38).
     * @param dizzy    RGEController dizzy flag (field 0xa1).
     * @param outRoll  receives the [0,100) roll when the draw fires.
     * @return true if the draw fired (and outRoll was written), false if gated.
     */
    bool ShootReflectionRoll(bool canShoot, bool dead, bool dizzy,
                             int &outRoll);

    /**
     * @brief Apply a hit's resulting HP; enters the angry phase once at < 50%.
     *
     * FAITHFUL: BossAI12.GetHurt @ game_full.c:957378 -> BossAI12.BossAngry
     * @ 957423. Gate: ignored unless awake && !dead. The angry trigger is
     * hp/max_hp < 0.5 (max_hp at role_attribute+0x18, hp at +0x1c) AND
     * angry(0xcc) not yet set. BossAngry sets angry(0xcc)=1 and shoot_cd(0x3c)
     * *= 0.5 (animator speed/bool are owner-side). No RNG draw.
     */
    void OnHurt(bool awake, int hpAfter, int maxHp);

    /**
     * @brief Get stunned: latch dizzy and drop the walk animation.
     *
     * FAITHFUL: BossAI12.Dizzy @ game_full.c:957800. Gated by !dead(0x38): only
     * then is dizzy(0xa1) set to 1 (anim.SetBool("walk", false) is owner-side).
     * No RNG draw.
     */
    void OnDizzy();

    /**
     * @brief InAtk01 bullet count: angry ? 2 : 1.
     *
     * FAITHFUL: BossAI12.InAtk01 @ game_full.c:957587. Reads angry(0xcc) to pick
     * the instantiate count; the (-n <= n) guard in the decomp is always true.
     * No state write, no RNG draw (Instantiate/PlayEffect are owner-side).
     */
    int InAtk01BulletCount() const;

    /**
     * @brief InAtk03 angle step in degrees: 180 / (angry ? 6 : 5).
     *
     * FAITHFUL: BossAI12.InAtk03 @ game_full.c:957718 (__udivsi3(0xb4, divisor),
     * divisor = angry(0xcc) ? 6 : 5). No state write, no RNG draw.
     */
    int InAtk03AngleStep() const;

    /**
     * @brief EndAtk04 cadence relax on the attack-param struct.
     *
     * FAITHFUL: BossAI12.EndAtk04 @ game_full.c:957781 (param[0x1c]->field 0x14
     * -= 1.0). Applies the -1.0 delta to a provided shoot_cd-like scalar and
     * returns it (the indirect tail call is owner-side). No RNG draw.
     */
    float EndAtk04ShootCd(float current) const;

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Angry = false;     ///< field 0xcc
    bool m_Dizzy = false;     ///< field 0xa1
    bool m_Dead = false;      ///< field 0x38 (driven via SetDead)
    bool m_HasTarget = true;  ///< field 0x7c != 0 (Scout clears it)
    float m_ShootCd = 0.0F;   ///< field 0x3c (halved by BossAngry)
};

} // namespace Game

#endif /* GAME_BOSS_AI12_HPP */
