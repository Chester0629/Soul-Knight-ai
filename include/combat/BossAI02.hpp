#ifndef GAME_BOSS_AI02_HPP
#define GAME_BOSS_AI02_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI02
 * @brief Faithful phase/cadence brain for the second boss (RGEController subclass).
 *
 * Per-content port: a boss-specific logic layer over the deterministic
 * RGEController/RGRandom base. Models only the pure, unit-testable brain; the
 * audio (RGMusicManager.PlayEffect), animator triggers, weapon/laser spawns
 * (Instantiate<RGWeapon>/RGELaser), transfer-gate VFX and Invoke() chains are
 * owning-entity concerns and are left out. Modelled here, all FAITHFUL to the
 * decomp at game_full.c:437291-437937:
 *   - the single angry-phase transition at hp/max_hp < 0.5 (BossAngry, once):
 *     unlike BossAI01 this does NOT touch shoot_cd; it adds +0.2 to
 *     role_attribute.speed (offset 0x14) and sets animator speed = 1.2;
 *   - the deterministic attack selection (ShootReflection's Range(0,100) roll);
 *   - the random wander direction (RunReflection's Range(-1,1) x2, normalized);
 *   - the per-attack state writes (shooting / can_shoot / weapon_lock_target)
 *     and the per-attack RNG draws that keep the stream in lockstep:
 *       Atk02 -> CreateBullet Range(-60, 60)  (bullet angle/spread),
 *       EndAtk03 -> Range(0, 10),
 *       InAtk04 -> Range(0, 3).
 *
 * @see recreation BossAI01.{hpp,cpp}; skeleton BossAI02.cs.
 *      FAITHFUL: BossAI02 @ game_full.c:437291-437921.
 */
class BossAI02 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// role_attribute.speed bonus applied on entering angry (BossAngry: +0.2).
    static constexpr float kAngrySpeedBonus = 0.2F;
    /// Animator speed on entering angry (BossAngry: set_speed 1.2f / 0x3f99999a).
    static constexpr float kAngryAnimSpeed = 1.2F;
    /// Number of attack animations (InAtk01..InAtk04).
    static constexpr int kAttackCount = 4;
    /// Roll ceiling used by ShootReflection (Range(0, 100), max exclusive).
    static constexpr int kRollCeiling = 100;
    /// Atk02 bullet angle spread bounds (CreateBullet: Range(-60, 60)).
    static constexpr int kBulletAngleMin = -60;
    static constexpr int kBulletAngleMax = 60; // max EXCLUSIVE (Unity int)
    /// EndAtk03 follow-up roll ceiling (Range(0, 10), max exclusive).
    static constexpr int kEndAtk03RollCeiling = 10;
    /// InAtk04 roll ceiling (Range(0, 3), max exclusive).
    static constexpr int kInAtk04RollCeiling = 3;

    BossAI02() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Apply a hit's resulting HP; enters the angry phase once at < 50%.
     * FAITHFUL: BossAI02__BossAngry @ game_full.c:437520 (field 0xd0 = 1;
     * role_attribute.speed += 0.2; anim.speed = 1.2). The hp/max_hp < 0.5 gate
     * is the standard boss GetHurt->BossAngry trigger (GetHurt body is in the
     * shared DamageSystem; only the BossAngry effects are modelled here).
     */
    void OnHurt(int hpAfter, int maxHp);

    bool Angry() const { return m_Angry; }
    /// Accumulated angry speed bonus (0 while calm, kAngrySpeedBonus once angry).
    float SpeedBonus() const { return m_SpeedBonus; }
    float AnimSpeed() const { return m_AnimSpeed; }

    bool Shooting() const { return m_Shooting; }
    bool CanShoot() const { return m_CanShoot; }
    bool WeaponLockTarget() const { return m_WeaponLockTarget; }

    /**
     * @brief Roll-and-select one attack: the deterministic attack selection.
     *
     * FAITHFUL to ShootReflection's single `rg_random.Range(0, 100)` draw
     * (game_full.c:437471). The roll->attack jumptable could not be recovered
     * from the decomp (indirect jump at 0x53b460, "Could not recover jumptable
     * ... Too many branches"), so the 4 equal buckets here are a reconstruction
     * (see fabricationFlags). The single Range(0,100) draw keeps the stream in
     * lockstep with the original regardless of bucketing. Like BossAI01, this
     * returns a bucket index but does NOT persist it: the decomp's InAtkNN hooks
     * never write an atk_index field.
     * @return chosen attack bucket in [0, kAttackCount).
     */
    int ChooseAttack();

    /**
     * @brief Random wander direction (RunReflection: Range(-1,1) x2, normalized).
     * FAITHFUL: BossAI02__RunReflection @ game_full.c:437433/437438
     * (two Range(-1.0f, 1.0f) draws, built into a Vector2 then normalized into
     * move_direction).
     */
    glm::vec2 WanderDirection();

    /**
     * @brief Begin Atk01 (InAtk01): shooting = true, weapon_lock_target = false.
     * FAITHFUL: BossAI02__InAtk01 @ game_full.c:437645/437658. No RNG draw.
     * (owner: PlayEffect, zero rigidbody velocity, spawn RGWeapon/RGELaser.)
     */
    void InAtk01();

    /**
     * @brief Begin Atk02 (InAtk02 -> CreateBullet): shooting = true; draws the
     * bullet angle. FAITHFUL: BossAI02__InAtk02 @ 437705 then
     * BossAI02__CreateBullet @ game_full.c:437736 (Range(-60, 60)).
     * @return bullet angle/spread roll in [kBulletAngleMin, kBulletAngleMax).
     * (owner: PlayEffect, zero rigidbody velocity, spawn bullets at the angle.)
     */
    int InAtk02();

    /**
     * @brief End Atk02 (EndAtk02): shooting = false, can_shoot = true.
     * FAITHFUL: BossAI02__EndAtk02 @ game_full.c:437753/437765. No RNG draw.
     * (owner: reset hand localEulerAngles to 0, Invoke("TrunWeaponLock", 0.1f).)
     */
    void EndAtk02();

    /**
     * @brief End Atk03 (EndAtk03): weapon_lock_target = false, can_shoot = true;
     * draws the follow-up roll. FAITHFUL: BossAI02__EndAtk03 @
     * game_full.c:437843/437854/437860 (Range(0, 10)).
     * @return follow-up roll in [0, kEndAtk03RollCeiling).
     * (owner: reset hand localEulerAngles to 0.)
     */
    int EndAtk03();

    /**
     * @brief Begin Atk04 (InAtk04): draws the variant roll.
     * FAITHFUL: BossAI02__InAtk04 @ game_full.c:437895 (Range(0, 3)). The decomp
     * body writes NO state field (only PlayEffect, which is owner-side), so this
     * draws RNG only -- no shooting/lock/atk_index write.
     * @return variant roll in [0, kInAtk04RollCeiling).
     */
    int InAtk04();

    /**
     * @brief End Atk04 (EndAtk04): weapon_lock_target = false, can_shoot = true.
     * FAITHFUL: BossAI02__EndAtk04 @ game_full.c:437908/437919. No RNG draw.
     * (owner: reset hand localEulerAngles to 0.)
     */
    void EndAtk04();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Angry = false;
    float m_SpeedBonus = 0.0F;
    float m_AnimSpeed = 1.0F;
    bool m_Shooting = false;
    bool m_CanShoot = false;
    bool m_WeaponLockTarget = false;
};

} // namespace Game

#endif /* GAME_BOSS_AI02_HPP */
