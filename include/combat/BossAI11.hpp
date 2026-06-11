#ifndef GAME_BOSS_AI11_HPP
#define GAME_BOSS_AI11_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI11
 * @brief Faithful state/cadence brain for boss #11 (RGEController subclass).
 *
 * Per-content port (faithful-port report): a boss-specific logic layer over the
 * deterministic RGEController/RGRandom base. Models only the pure, unit-testable
 * brain; the animator triggers (SetBool/set_speed), bullet/weapon spawns
 * (Instantiate<RGWeapon>, PrefabPool), the transfer-gate VFX, rigidbody velocity
 * integration, transform reads and Invoke()/coroutine chains are owning-entity
 * concerns and are left out. Modelled here, all FAITHFUL to the decomp at
 * game_full.c:956663-957145:
 *   - the FixedUpdate movement gate (awake && !shooting && !dead) plus the single
 *     field write it performs when dead-while-shooting (awake = false);
 *   - the Scout target-clear gate (!dizzy && !dead -> target_obj = null);
 *   - the RunReflection wander direction (two Range(-1,1) draws, normalized);
 *   - the ShootReflection attack-roll gate (can_shoot && !dead && !dizzy ->
 *     Range(0,100)); the roll->InAtkNN jumptable was NOT recovered from the
 *     decomp (indirect jump at 0x00ad4684, "Could not recover jumptable. Too
 *     many branches"), so only the single draw is modelled (stream lockstep);
 *   - the angry-phase transition at hp/max_hp < 0.5 (GetHurt -> BossAngry, once):
 *     angry(0xd1) = true and shoot_cd(0x3c) *= 0.5 (faster shooting in phase 2);
 *   - the per-attack state writes that the decomp actually performs:
 *       InAtk03 -> weapon_lock_target(0x1c) = true (no RNG),
 *       IntAtkIn -> gated only on angry(0xd1) (spawn is owner-side, no RNG),
 *   - the Dizzy stun latch (!dead -> dizzy(0xa1) = true).
 *
 * RNG DRAWS (exact count + order, stream lockstep):
 *   - RunReflection: 2x Range(-1.0f, 1.0f)   (float, max INCLUSIVE)
 *   - ShootReflection (gate open): 1x Range(0, 100)  (int, max EXCLUSIVE)
 * Every other modelled body takes NO draw; gated-out paths take NO draw.
 *
 * @see recreation RGEController.cs (offset table); skeleton BossAI11.cs.
 *      FAITHFUL: BossAI11 @ game_full.c:956663-957145.
 */
class BossAI11 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (GetHurt->BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// shoot_cd multiplier applied once on entering angry (BossAngry: *= 0.5).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// Animator speed set on entering angry (set_speed 1.2f / 0x3f99999a); owner-applied.
    static constexpr float kAngryAnimSpeed = 1.2F;
    /// Wander direction component bounds (RunReflection: Range(-1.0f, 1.0f) x2).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F; // max INCLUSIVE (Unity float)
    /// Attack-selection roll ceiling (ShootReflection: Range(0, 100), max exclusive).
    static constexpr int kRollCeiling = 100;

    BossAI11() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- observable state (decoded field offsets) ---------------------------
    bool Awake() const { return m_Awake; }              // 0x18
    bool Dead() const { return m_Dead; }                // 0x38
    bool Shooting() const { return m_Shooting; }        // 0x80
    bool CanShoot() const { return m_CanShoot; }        // 0x40
    bool Dizzy() const { return m_Dizzy; }              // 0xa1
    bool Angry() const { return m_Angry; }              // 0xd1
    bool WeaponLockTarget() const { return m_WeaponLockTarget; } // 0x1c
    bool HasTarget() const { return m_HasTarget; }      // 0x7c (target_obj != null)
    float ShootCd() const { return m_ShootCd; }         // 0x3c
    float AnimSpeed() const { return m_AnimSpeed; }     // anim.speed (owner mirror)

    /// Test/spawn setup of the mutable gate fields the decomp reads.
    void SetAwake(bool v) { m_Awake = v; }
    void SetDead(bool v) { m_Dead = v; }
    void SetShooting(bool v) { m_Shooting = v; }
    void SetCanShoot(bool v) { m_CanShoot = v; }
    void SetTarget(bool hasTarget) { m_HasTarget = hasTarget; }
    void SetShootCd(float cd) { m_ShootCd = cd; }

    /**
     * @brief FixedUpdate movement gate + the one field write it performs.
     *
     * FAITHFUL: BossAI11__FixedUpdate @ game_full.c:956676. The body integrates
     * rigidbody velocity = move_direction * speed * (friction + 1) (owner-side,
     * not modelled). The ONLY brain-observable field write is the dead-recovery
     * branch: when awake(0x18) && !shooting(0x80) && dead(0x38), it sets
     * awake(0x18) = false (and zeroes velocity, owner-side). No RNG draw.
     * @return true if the per-fixed-step move path ran (awake && !shooting).
     */
    bool FixedUpdate();

    /**
     * @brief Scout gate: clears the chase target when not stunned and not dead.
     *
     * FAITHFUL: BossAI11__Scout @ game_full.c:956747. Gate is !dizzy(0xa1) &&
     * !dead(0x38); inside it the decomp writes target_obj(0x7c) = null (then
     * reads transform, owner-side). No RNG draw.
     * @return true if the gate opened (target was cleared).
     */
    bool Scout();

    /**
     * @brief RunReflection wander direction: two Range(-1,1) draws, normalized.
     *
     * FAITHFUL: BossAI11__RunReflection @ game_full.c:956772 (two
     * RGRandom::Range(-1.0f, 1.0f) draws at 0xbf800000/0x3f800000, built into a
     * Vector2, normalized, written to move_direction; then anim.SetBool("walk")
     * owner-side). Draws exactly two floats in order x then y (stream lockstep).
     * @return normalized wander direction (zero vector if both draws are zero).
     */
    glm::vec2 WanderDirection();

    /**
     * @brief ShootReflection attack-selection gate + roll.
     *
     * FAITHFUL: BossAI11__ShootReflection @ game_full.c:956830. The gate is
     * can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1). When OPEN it draws exactly
     * one RGRandom::Range(0, 100); the roll->InAtkNN jumptable was NOT recovered
     * (indirect jump at 0x00ad4684, "Could not recover jumptable. Too many
     * branches"), so the bucketing is intentionally NOT modelled -- only the
     * single draw is taken to keep the stream in lockstep. When the gate is
     * CLOSED, NO draw is taken.
     * @return the raw roll in [0, kRollCeiling) when the gate is open, or -1 when
     *         the gate is closed (no draw taken).
     */
    int ShootReflection();

    /**
     * @brief Apply a hit's resulting HP; enters the angry phase once at < 50%.
     *
     * FAITHFUL: BossAI11__GetHurt @ game_full.c:956898 -> BossAI11__BossAngry @
     * game_full.c:956940. Gates: requires awake(0x18) and !dead(0x38) (an
     * un-awake or dead boss ignores the hit). The angry trigger is
     * hp(0x1c)/max_hp(0x18) < 0.5 && !angry(0xd1). On trigger BossAngry sets
     * angry(0xd1) = true and shoot_cd(0x3c) *= 0.5 (anim.set_speed(1.2f) and
     * anim.SetBool("angry") are owner-side). No RNG draw.
     */
    void OnHurt(int hpAfter, int maxHp);

    /**
     * @brief Begin Atk03 (InAtk03): weapon_lock_target = true.
     * FAITHFUL: BossAI11__InAtk03 @ game_full.c:957038 (writes
     * weapon_lock_target(0x1c) = 1, then Instantiate<RGWeapon> owner-side).
     * No RNG draw.
     */
    void InAtk03();

    /**
     * @brief Intro-attack gate (IntAtkIn): true only while angry.
     * FAITHFUL: BossAI11__IntAtkIn @ game_full.c:957069. The only brain-visible
     * branch is gated on angry(0xd1): when angry it spawns an extra prefab
     * (owner-side); regardless it then Instantiate<RGWeapon> (owner-side).
     * Writes no field and draws no RNG; exposed so the owner can branch.
     * @return true if angry(0xd1) (the extra-spawn branch is taken).
     */
    bool IntAtkIn() const { return m_Angry; }

    /**
     * @brief Stun latch (Dizzy): set dizzy = true while not dead.
     * FAITHFUL: BossAI11__Dizzy @ game_full.c:957124. Gate is !dead(0x38); inside
     * it the decomp sets dizzy(0xa1) = true (then anim.SetBool("walk", false)
     * owner-side). No RNG draw.
     * @return true if the gate opened (dizzy was latched).
     */
    bool MakeDizzy();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Awake = false;             // 0x18
    bool m_Dead = false;              // 0x38
    bool m_Shooting = false;          // 0x80
    bool m_CanShoot = false;          // 0x40
    bool m_Dizzy = false;             // 0xa1
    bool m_Angry = false;             // 0xd1
    bool m_WeaponLockTarget = false;  // 0x1c
    bool m_HasTarget = false;         // 0x7c (target_obj != null)
    float m_ShootCd = 0.0F;           // 0x3c
    float m_AnimSpeed = 1.0F;         // anim.speed (owner mirror)
};

} // namespace Game

#endif /* GAME_BOSS_AI11_HPP */
