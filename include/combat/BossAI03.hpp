#ifndef GAME_BOSS_AI03_HPP
#define GAME_BOSS_AI03_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI03
 * @brief Faithful phase/cadence brain for the third boss (RGEController subclass).
 *
 * Per-content port (report style of BossAI02/BossAI04): a boss-specific logic
 * layer over the deterministic RGEController/RGRandom base. Models only the pure,
 * unit-testable brain; the audio (RGMusicManager.PlayEffect/StopBgm), animator
 * triggers/SetBool/set_speed, rigidbody velocity, weapon/bullet spawns
 * (Instantiate<RGWeapon>/GetComponent<RGBullet>), the transfer-gate VFX, the
 * BossInfo HP bar and the Invoke()/transform reads are owning-entity concerns
 * and are left out. Everything below is FAITHFUL to the decomp at
 * game_full.c:437925-438829.
 *
 * Modelled brain:
 *   - the random wander direction (RunReflection: Range(-1,1) x2, normalized
 *     into move_direction);
 *   - the deterministic attack-selection roll (ShootReflection's single
 *     Range(0,100), gated by can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1));
 *   - the per-attack START state writes the decomp actually performs:
 *       StartAtk02 -> weapon_lock_target(0x1c)=1, shooting(0x80)=1,
 *       StartAtk03/StartAtk04/StartAtk05 -> shooting(0x80)=1,
 *       StartAtk04 also clears atk4_index(0xdc)=0;
 *   - the per-attack END writes + follow-up rolls:
 *       EndAtk01 -> weapon_lock_target(0x1c)=0, can_shoot(0x40)=1, Range(0,100),
 *       EndAtk02 -> weapon_lock_target(0x1c)=0, can_shoot(0x40)=1, then a
 *         gate on angry(0xe0): only when angry does it draw Range(0,100);
 *   - InAtk02/InAtk05 -> shooting(0x80)=0 (the bullet-release frame);
 *   - the single angry-phase transition (BossAngry, once): angry(0xe0)=1
 *     (+ anim.set_speed 1.2 / SetBool "angry"), gated hp/max_hp < 0.5;
 *   - ChildDead -> shooting(0x80)=0; Dizzy -> if !dead(0x38) set dizzy(0xa1)=1;
 *   - Scout gate: a no-op unless !dead(0x38) && !dizzy(0xa1).
 *
 * No InAtkNN here writes atk_index: the original tracks attack state only via
 * the animator/shooting/lock flags, so this brain does the same (the BossAI02
 * pilot lesson: do not invent atk_index writes).
 *
 * @see metadata BossAI03.cs (field names: atk4_index, angry, boss_clip ...);
 *      FAITHFUL: BossAI03 @ game_full.c:437925-438829
 *      (boss03: RGEController subclass; offsets via RGEController.cs).
 */
class BossAI03 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// Number of distinct attacks (StartAtk01..StartAtk05 / InAtk01..InAtk05).
    static constexpr int kAttackCount = 5;
    /// Idle / no-attack sentinel (no decomp field; brain bookkeeping only).
    static constexpr int kNoAttack = 0;
    /// Roll ceiling used by ShootReflection / EndAtk01 / EndAtk02 (Range(0,100)).
    static constexpr int kRollCeiling = 100;
    /// RunReflection wander component bounds (Range(-1f, 1f), max INCLUSIVE).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// Animator speed on entering angry (BossAngry: set_speed 1.2f / 0x3f99999a).
    static constexpr float kAngryAnimSpeed = 1.2F;

    BossAI03() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- gate / vitals state ------------------------------------------------

    bool Dead() const { return m_Dead; }
    bool Dizzy() const { return m_Dizzy; }
    bool Angry() const { return m_Angry; }
    bool Shooting() const { return m_Shooting; }
    bool CanShoot() const { return m_CanShoot; }
    bool WeaponLockTarget() const { return m_WeaponLockTarget; }
    int Atk4Index() const { return m_Atk4Index; }
    float AnimSpeed() const { return m_AnimSpeed; }

    /// Active attack bucket bookkeeping (0 = idle; 1..5 once chosen).
    int AtkIndex() const { return m_AtkIndex; }

    /// Test/owner hook: latch the death flag (mirrors RGEController.Dead 0x38).
    void SetDead(bool dead) { m_Dead = dead; }

    /**
     * @brief Apply a hit's resulting HP; enters the angry phase once at < 50%.
     *
     * FAITHFUL: BossAI03__BossAngry @ game_full.c:438309 (field 0xe0 angry = 1;
     * anim.set_speed(1.2f); anim.SetBool("angry", true)). The hp/max_hp < 0.5
     * gate is the standard boss GetHurt->BossAngry trigger (GetHurt body is in
     * the shared DamageSystem; only the BossAngry effects are modelled here).
     */
    void OnHurt(int hpAfter, int maxHp);

    /**
     * @brief Stun the boss (Dizzy). FAITHFUL: BossAI03__Dizzy @
     * game_full.c:438810: only when !dead(0x38) does it set dizzy(0xa1)=1
     * (+ anim.SetBool("run", false)). No RNG draw.
     * @return true if the stun latched (was not dead).
     */
    bool DizzyHit();

    /**
     * @brief A body-part died. FAITHFUL: BossAI03__ChildDead @
     * game_full.c:438332: shooting(0x80)=0 (+ Invoke("CreateTransferGate"),
     * StopBgm). No RNG draw.
     */
    void ChildDead();

    // ---- per-frame reflections ----------------------------------------------

    /**
     * @brief Random wander direction (RunReflection: Range(-1,1) x2, normalized).
     *
     * FAITHFUL: BossAI03__RunReflection @ game_full.c:438034 (two
     * Range(-1.0f, 1.0f) draws built into a Vector2, normalized into
     * move_direction; then anim.SetBool("run", true) -- owner). Draws TWO floats
     * in order (x then y) to keep the stream in lockstep.
     */
    glm::vec2 WanderDirection();

    /**
     * @brief Roll-and-select one attack: the deterministic attack selection.
     *
     * FAITHFUL: BossAI03__ShootReflection @ game_full.c:438092. The draw is
     * GATED: only when can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1) does the
     * original take the single `rg_random.Range(0, 100)` draw; otherwise the
     * gated-out path takes NO draw (stream lockstep). The roll->StartAtkNN
     * jumptable could not be recovered from the decomp (indirect jump at
     * 0x53ea2c, "Could not recover jumptable ... Too many branches"), so the 5
     * equal buckets here are a reconstruction. The single Range(0,100) draw keeps
     * the stream lockstep regardless of bucketing. Sets/returns AtkIndex in
     * [1, kAttackCount]; returns kNoAttack and draws nothing when gated out.
     * @return chosen attack bucket in [1, kAttackCount], or kNoAttack if gated.
     */
    int ChooseAttack();

    // ---- attack START hooks (state writes only; spawns are owner-side) ------

    /**
     * @brief Begin Atk02. FAITHFUL: BossAI03__StartAtk02 @ game_full.c:438121:
     * weapon_lock_target(0x1c)=1, shooting(0x80)=1 (+ PlayEffect, SetTrigger).
     * No RNG draw.
     */
    void StartAtk02();

    /**
     * @brief Begin Atk03. FAITHFUL: BossAI03__StartAtk03 @ game_full.c:438159:
     * shooting(0x80)=1 (+ SetTrigger, zero rigidbody velocity, PlayEffect).
     * No RNG draw.
     */
    void StartAtk03();

    /**
     * @brief Begin Atk04. FAITHFUL: BossAI03__StartAtk04 @ game_full.c:438209:
     * shooting(0x80)=1, atk4_index(0xdc)=0 (+ PlayEffect, SetTrigger, zero
     * move_direction). No RNG draw.
     */
    void StartAtk04();

    /**
     * @brief Begin Atk05. FAITHFUL: BossAI03__StartAtk05 @ game_full.c:438255:
     * shooting(0x80)=1 (+ SetTrigger). No RNG draw.
     */
    void StartAtk05();

    // ---- attack IN/END hooks ------------------------------------------------

    /**
     * @brief Atk02 bullet-release frame. FAITHFUL: BossAI03__InAtk02 @
     * game_full.c:438466: shooting(0x80)=0 (+ PlayEffect, zero rigidbody
     * velocity, spawn bullet02). No RNG draw.
     */
    void InAtk02();

    /**
     * @brief Atk05 bullet-release frame. FAITHFUL: BossAI03__InAtk05 @
     * game_full.c:438684: writes NO state field directly (PlayEffect, zero
     * rigidbody velocity, spawn bullet05 are all owner-side). No RNG draw.
     *
     * Provided as a no-op hook for symmetry: included to document that the
     * decomp body performs no brain-side write (unlike InAtk02). Kept so callers
     * can route the InAtk05 animation event without inventing state.
     */
    void InAtk05();

    /**
     * @brief End Atk01. FAITHFUL: BossAI03__EndAtk01 @ game_full.c:438421:
     * weapon_lock_target(0x1c)=0, can_shoot(0x40)=1 (+ reset two hand
     * localEulerAngles to 0), then a single Range(0,100) follow-up roll.
     * @return follow-up roll in [0, kRollCeiling).
     */
    int EndAtk01();

    /**
     * @brief End Atk02. FAITHFUL: BossAI03__EndAtk02 @ game_full.c:438525:
     * weapon_lock_target(0x1c)=0, can_shoot(0x40)=1 (+ reset two hand
     * localEulerAngles to 0). The follow-up roll is GATED on angry(0xe0): only
     * when angry does it draw Range(0,100); the calm path takes NO draw and falls
     * through to a virtual tail-call (owner). Stream stays in lockstep.
     * @return follow-up roll in [0, kRollCeiling) when angry, else -1 (no draw).
     */
    int EndAtk02();

    /**
     * @brief Stop the active attack bookkeeping (AtkIndex -> kNoAttack).
     *
     * Brain-side convenience matching the BossAI04 StopAttack shape; the decomp
     * clears no atk_index field, so this only resets the brain's bucket bookkeeping
     * (it performs no field write the decomp omits and draws no RNG).
     */
    void StopAttack();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Dead = false;
    bool m_Dizzy = false;
    bool m_Angry = false;
    bool m_Shooting = false;
    bool m_CanShoot = false;
    bool m_WeaponLockTarget = false;
    int m_Atk4Index = 0;
    float m_AnimSpeed = 1.0F;
    int m_AtkIndex = kNoAttack; ///< brain bookkeeping only (no decomp field).
};

} // namespace Game

#endif /* GAME_BOSS_AI03_HPP */
