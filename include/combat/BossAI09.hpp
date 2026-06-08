#ifndef GAME_BOSS_AI09_HPP
#define GAME_BOSS_AI09_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI09
 * @brief Faithful phase/cadence brain for the ninth boss (RGEController subclass).
 *
 * Per-content port (report #4/#5): a boss-specific logic layer over the
 * deterministic RGEController/RGRandom base. Models only the pure, unit-testable
 * brain; sprite swaps, animator triggers/SetBool, bullet/weapon spawns
 * (Instantiate<RGWeapon> + GetComponent<RGBullet>), the transfer-gate VFX
 * (ResourcesUtil.Load/Instantiate), Rigidbody2D velocity integration, transform
 * reads and Invoke() chains are owning-entity concerns and are left out.
 *
 * Modelled here, all FAITHFUL to the decomp at game_full.c:443054-443720:
 *   - the random wander direction (RunReflection: Range(-1,1) x2, normalized
 *     into move_direction), gated by nothing in the RNG path;
 *   - the deterministic attack-selection roll (ShootReflection's single
 *     Range(0,100)), gated by can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1) -
 *     a gated-out roll takes NO draw (stream lockstep);
 *   - the single angry-phase transition (BossAngry, once): angry flag(0xd0) = 1,
 *     shoot_cd(0x3c) *= 0.5 (HALVED, not additive), animator speed = 1.2;
 *   - the move-zeroing attack starts (StartAtk02 / StartAtk03 / InAtk04 each
 *     set move_direction = Vector2.zero);
 *   - Scout's target_obj(0x7c) = null when !dead && !dizzy;
 *   - Dizzy's dizzy(0xa1) = true when !dead;
 *   - FixedUpdate's awake(0x18) = 0 clear when awake && !shooting && dead (the
 *     death side of the awake latch; the rest of FixedUpdate is owner-side
 *     Rigidbody2D velocity integration + EnemyUpdate vtable dispatch).
 *
 * Field offsets decoded against recreation/Enemy/RGEController.cs:
 *   dead 0x38, can_shoot 0x40, shoot_cd 0x3c, awake 0x18, shooting 0x80,
 *   dizzy 0xa1, target_obj 0x7c, rg_random 0x0c, role_attribute 0x70.
 *   Angry latch lives at 0xd0 (boss-local field, same slot BossAI02 uses).
 *
 * @see recreation BossAI02.{hpp,cpp}, BossAI04.{hpp,cpp}.
 *      FAITHFUL: BossAI09 @ game_full.c:443054-443720.
 */
class BossAI09 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// Animator speed on entering angry (BossAngry: set_speed 1.2f / 0x3f99999a).
    static constexpr float kAngryAnimSpeed = 1.2F;
    /// shoot_cd multiplier applied on entering angry (BossAngry: shoot_cd *= 0.5).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// Number of attack animations (Atk01..Atk05 / StartAtkNN / InAtkNN).
    static constexpr int kAttackCount = 5;
    /// Idle / no-attack sentinel for the chosen-attack bucket.
    static constexpr int kNoAttack = 0;
    /// Roll ceiling used by ShootReflection (Range(0, 100), max exclusive).
    static constexpr int kRollCeiling = 100;
    /// RunReflection wander-component bounds (Range(-1f, 1f), max inclusive).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;

    BossAI09() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- gate inputs (mirror the controller fields the decomp reads) --------
    void SetDead(bool dead) { m_Dead = dead; }
    void SetDizzy(bool dizzy) { m_Dizzy = dizzy; }
    void SetCanShoot(bool canShoot) { m_CanShoot = canShoot; }
    void SetShooting(bool shooting) { m_Shooting = shooting; }
    void SetAwake(bool awake) { m_Awake = awake; }

    bool Dead() const { return m_Dead; }
    bool Dizzy() const { return m_Dizzy; }
    bool CanShoot() const { return m_CanShoot; }
    bool Shooting() const { return m_Shooting; }
    bool Awake() const { return m_Awake; }
    bool Angry() const { return m_Angry; }
    float ShootCd() const { return m_ShootCd; }
    float AnimSpeed() const { return m_AnimSpeed; }
    bool TargetCleared() const { return m_TargetCleared; }
    /// Last move_direction written by Run/StartAtk/InAtk (Vector2 backing field).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }

    /**
     * @brief Initial shoot_cd before any angry scaling (controller field 0x3c).
     * BossAngry multiplies this in place; expose a setter so a caller can wire
     * the boss's configured cadence and observe the halving.
     */
    void SetShootCd(float shootCd) { m_ShootCd = shootCd; }

    /**
     * @brief Random wander direction (RunReflection: Range(-1,1) x2, normalized).
     *
     * FAITHFUL: BossAI09__RunReflection @ game_full.c:443219/443225 - two
     * Range(-1.0f, 1.0f) draws (0xbf800000/0x3f800000), built into a Vector2,
     * normalized, written into move_direction via set_move_direction. The
     * preceding target_obj position read and the animator SetBool are owner-side.
     * Always draws exactly two floats (no gate on the RNG path).
     */
    glm::vec2 RunReflection();

    /**
     * @brief Roll-and-select one attack: the deterministic attack selection.
     *
     * FAITHFUL: BossAI09__ShootReflection @ game_full.c:443256 - a single
     * `rg_random.Range(0, 100)` draw, GATED by can_shoot(0x40) != 0 &&
     * dead(0x38) == 0 && dizzy(0xa1) == 0. When the gate fails the original takes
     * the indirect tail call without drawing, so this consumes NO draw and
     * returns kNoAttack (stream stays in lockstep).
     *
     * The roll->StartAtkNN jumptable was NOT recovered from the decomp ("Could
     * not recover jumptable at 0x00557c98. Too many branches"); the 5 equal
     * buckets here are a reconstruction. The single Range(0,100) draw keeps the
     * stream deterministic regardless of bucketing.
     * @return chosen attack in [1, kAttackCount], or kNoAttack(0) when gated out.
     */
    int ShootReflection();

    /**
     * @brief Begin Atk02 (StartAtk02): move_direction = Vector2.zero.
     * FAITHFUL: BossAI09__StartAtk02 @ game_full.c:443300/443305. No RNG draw.
     * (owner: animator SetTrigger.)
     */
    void StartAtk02();

    /**
     * @brief Begin Atk03 (StartAtk03): move_direction = Vector2.zero.
     * FAITHFUL: BossAI09__StartAtk03 @ game_full.c:443327/443332. No RNG draw.
     * (owner: animator SetTrigger.)
     */
    void StartAtk03();

    /**
     * @brief Atk04 active frame (InAtk04): move_direction = Vector2.zero.
     * FAITHFUL: BossAI09__InAtk04 @ game_full.c:443625/443630. No RNG draw.
     * (owner: Instantiate<RGWeapon> + get_transform for the spawned bullet.)
     */
    void InAtk04();

    /**
     * @brief Scout tick: clears the chase target when alive and not stunned.
     * FAITHFUL: BossAI09__Scout @ game_full.c:443158 - when dead(0x38) == 0 &&
     * dizzy(0xa1) == 0, target_obj(0x7c) = null (then owner get_transform). No
     * RNG draw. Gated out (dead or dizzy) writes nothing.
     */
    void Scout();

    /**
     * @brief Enter the dizzy/stun state (Dizzy): dizzy(0xa1) = true when alive.
     * FAITHFUL: BossAI09__Dizzy @ game_full.c:443693 - when dead(0x38) == 0:
     * dizzy(0xa1) = 1, animator SetBool("run", false). No RNG draw; gated out
     * (dead) writes nothing.
     */
    void EnterDizzy();

    /**
     * @brief Wake transition (OnGameStateChange game_state == 1, room ready):
     * awake(0x18) = 1, then virtual StartBossAI.
     * FAITHFUL: BossAI09__OnGameStateChange @ game_full.c:443482 - sets the awake
     * latch when the maker's room reports state == 1. The room-ready chain
     * (the_maker.the_room.field(0x10) == 1) and the StartBossAI vtable call are
     * owner-side; only the awake field write is modelled. No RNG draw.
     * @param gameState global game-state code (1 == room start).
     * @param roomReady the_maker.the_room.state == 1 (decoded by the owner).
     */
    void OnGameStateChange(int gameState, bool roomReady);

    /**
     * @brief FixedUpdate's brain-side awake-clear (the death side of the latch).
     *
     * FAITHFUL: BossAI09__FixedUpdate @ game_full.c:443067 - inside
     * `if (awake(0x18) != 0)` then `if (shooting(0x80) == 0)`, when dead(0x38) != 0
     * the decomp writes `*(byte*)(this+0x18) = 0` (clears awake) before zeroing the
     * Rigidbody2D velocity. OnGameStateChange models the awake SET; this models the
     * matching CLEAR so the latch is symmetric.
     *
     * The remainder of FixedUpdate - RGEController::FixedUpdateSeed, the
     * Rigidbody2D velocity integration from move_direction*speed, and the
     * EnemyUpdate vtable(+0x11c) dispatch under `if (dead == 0)` - is owner-side
     * and involves NO RNG draw, so it is left out.
     */
    void FixedUpdateTick();

    /**
     * @brief Apply a hit's resulting HP; enters the angry phase once at < 50%.
     * FAITHFUL: BossAI09__BossAngry @ game_full.c:443413 - angry flag(0xd0) = 1,
     * shoot_cd(0x3c) *= 0.5, animator set_speed(1.2f), SetBool("angry", true).
     * The hp/max_hp < 0.5 gate is the standard boss GetHurt->BossAngry trigger
     * (GetHurt lives in the shared DamageSystem; only the BossAngry field effects
     * are modelled here). No RNG draw.
     */
    void OnHurt(int hpAfter, int maxHp);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Dead = false;
    bool m_Dizzy = false;
    bool m_CanShoot = false;
    bool m_Shooting = false;
    bool m_Awake = false;
    bool m_Angry = false;
    bool m_TargetCleared = false;
    float m_ShootCd = 0.0F;
    float m_AnimSpeed = 1.0F;
    glm::vec2 m_MoveDirection{0.0F, 0.0F};
};

} // namespace Game

#endif /* GAME_BOSS_AI09_HPP */
