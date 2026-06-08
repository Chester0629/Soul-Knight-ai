#ifndef GAME_BOSS_AI08_HPP
#define GAME_BOSS_AI08_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI08
 * @brief Faithful phase/cadence brain for the eighth boss (RGEController subclass).
 *
 * Per-content port: a boss-specific logic layer over the deterministic
 * RGEController/RGRandom base. Models ONLY the pure, unit-testable brain. The
 * audio (RGMusicManager.PlayEffect), animator triggers/SetBool/SetTrigger,
 * weapon/bullet spawns (Instantiate<RGWeapon>/RGBullet), the transfer-gate VFX,
 * rigidbody/transform writes and Invoke()/coroutine chains are owning-entity
 * concerns and are left out. Modelled here, all FAITHFUL to the decomp at
 * game_full.c:442447-443052:
 *   - the gated Scout target-clear: only when !dead(0x38) && !dizzy(0xa1) does
 *     it write target_obj(0x7c) = null (BossAI08__Scout, no RNG);
 *   - the random wander direction (RunReflection: two Range(-1,1) float draws,
 *     normalized into move_direction);
 *   - the deterministic attack-selection roll (ShootReflection's single
 *     Range(0,100)), GATED behind can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1)
 *     && rg_random!=null -- a gated-out path takes NO draw (stream lockstep);
 *   - the single angry-phase transition at hp/max_hp < 0.5 (BossAngry, once):
 *     angry(0xb0) = true AND shoot_cd(0x3c) *= 0.5 (animator speed=1.2 / "angry"
 *     bool are owner-side);
 *   - the weapon-lock state writes: StartAtk02 sets weapon_lock_target(0x1c)=1;
 *     TrunWeaponLock clears weapon_lock_target(0x1c)=0 (neither draws RNG).
 *
 * RNG DRAW SUMMARY (count + order must replay frame-for-frame):
 *   - RunReflection : Range(-1f,1f), Range(-1f,1f)   -> 2 float draws (always);
 *   - ShootReflection: Range(0,100)                  -> 1 int draw (gated);
 *   - all StartAtkNN/InAtkNN/Atk04Combo/CreateFireBall/CreateTransferGate/Start/
 *     FixedUpdate/FixedRotation/GetHurt bodies        -> 0 brain draws.
 *
 * @see recreation BossAI01.cs/BossAI04.cs; skeleton BossAI08.cs.
 *      FAITHFUL: BossAI08 @ game_full.c:442447-443052.
 */
class BossAI08 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// shoot_cd(0x3c) multiplier applied once on entering angry (BossAngry: *0.5).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// Animator speed on entering angry (BossAngry: set_speed 1.2f / 0x3f99999a).
    /// Owner-side (animator); exposed as a golden scalar only.
    static constexpr float kAngryAnimSpeed = 1.2F;
    /// Roll ceiling used by ShootReflection (Range(0, 100), max EXCLUSIVE).
    static constexpr int kRollCeiling = 100;
    /// RunReflection wander-direction bounds (Range(-1f, 1f), max INCLUSIVE).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;

    BossAI08() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// hp/max_hp < 0.5 ? (BossAI08 has no shoot-gate; this is the angry trigger).
    bool Angry() const { return m_Angry; }
    /// Live shoot cadence (seconds). Halved once on entering angry.
    float ShootCd() const { return m_ShootCd; }
    /// Initialise the live shoot cadence (RoleAttribute-backed shoot_cd, 0x3c).
    void SetShootCd(float seconds) { m_ShootCd = seconds; }

    bool WeaponLockTarget() const { return m_WeaponLockTarget; }
    /// True when Scout cleared the chase target (target_obj == null).
    bool TargetCleared() const { return m_TargetCleared; }

    /**
     * @brief BossAI08__Scout gate: clear the chase target while alive and not
     * stunned. FAITHFUL: BossAI08__Scout @ game_full.c:442527.
     *
     * Decode: gate `!dead(0x38) && !dizzy(0xa1)`; on the live path it writes
     * `target_obj(0x7c) = null` then get_transform (owner). Draws NO RNG.
     * @param dead  controller dead latch (0x38).
     * @param dizzy controller stun latch (0xa1).
     * @return true if the gate passed and target_obj was cleared.
     */
    bool Scout(bool dead, bool dizzy);

    /**
     * @brief Random wander direction (RunReflection: Range(-1,1) x2, normalized).
     * FAITHFUL: BossAI08__RunReflection @ game_full.c:442552 (two
     * Range(0xbf800000=-1f, 0x3f800000=1f) draws, built into a Vector2 then
     * normalized into move_direction; animator SetBool is owner-side).
     * @return normalized direction, or (0,0) on the degenerate zero-length roll.
     */
    glm::vec2 WanderDirection();

    /**
     * @brief Roll-and-select one attack: the deterministic attack selection.
     *
     * FAITHFUL: BossAI08__ShootReflection @ game_full.c:442610. The single
     * `rg_random.Range(0, 100)` draw is GATED behind
     *   can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1) && rg_random != null.
     * When the gate fails the original takes NO draw (it tail-calls the
     * jumptable directly) -- we faithfully consume no RNG on the gated path so
     * the stream stays in lockstep. The roll->StartAtkNN jumptable could not be
     * recovered from the decomp (indirect jump at 0x0055477c, "Could not recover
     * jumptable ... Too many branches"); we therefore return the raw roll and do
     * NOT persist an atk_index (no decomp body writes one).
     * @param canShoot can_shoot gate (0x40).
     * @param dead     dead latch (0x38).
     * @param dizzy    stun latch (0xa1).
     * @return the Range(0,100) roll in [0,100) when the gate passes, else -1
     *         (gated, no draw taken).
     */
    int ChooseAttack(bool canShoot, bool dead, bool dizzy);

    /**
     * @brief Apply a hit's resulting HP; enters the angry phase once at < 50%.
     *
     * FAITHFUL: BossAI08__GetHurt @ game_full.c:442723 -> BossAI08__BossAngry @
     * game_full.c:442664. The GetHurt body is gated `awake(0x18) && !dead(0x38)`,
     * delegates to the base RGEController__GetHurt (owner), then on
     * `hp/max_hp < 0.5 && !angry(0xb0)` runs BossAngry. BossAngry writes
     * angry(0xb0)=1 and shoot_cd(0x3c) *= 0.5 (animator speed/"angry" bool are
     * owner-side). UpDateBossHp is owner-side. Draws NO RNG.
     * @param awake controller awake latch (0x18); hits before awake are ignored.
     * @param dead  controller dead latch (0x38); hits after death are ignored.
     */
    void OnHurt(bool awake, bool dead, int hpAfter, int maxHp);

    /**
     * @brief Begin Atk02 (StartAtk02): weapon_lock_target = true.
     * FAITHFUL: BossAI08__StartAtk02 @ game_full.c:442643
     * (`*(byte*)(param_1+7) = 1` -> weapon_lock_target(0x1c) = 1; the
     * animator SetTrigger + indirect jumptable tail are owner-side). No RNG.
     */
    void StartAtk02();

    /**
     * @brief TrunWeaponLock (sic): weapon_lock_target = false.
     * FAITHFUL: BossAI08__TrunWeaponLock @ game_full.c:442903
     * (weapon_lock_target(0x1c) = 0; the hand localEulerAngles reset is
     * owner-side). No RNG.
     */
    void TrunWeaponLock();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Angry = false;
    float m_ShootCd = 0.0F;
    bool m_WeaponLockTarget = false;
    bool m_TargetCleared = false;
};

} // namespace Game

#endif /* GAME_BOSS_AI08_HPP */
