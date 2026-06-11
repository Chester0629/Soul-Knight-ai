#ifndef GAME_BOSS_AI07_HPP
#define GAME_BOSS_AI07_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI07
 * @brief Faithful gate/cadence/RNG brain for the seventh boss (RGEController
 *        subclass) of Soul Knight 1.7.10.
 *
 * Per-content port: a boss-specific logic layer over the deterministic
 * RGRandom stream. Models only the pure, unit-testable parts of the decompiled
 * state machine; Animator triggers, bullet/weapon spawns, audio PlayEffect,
 * transform reads, Instantiate, Invoke chains and vtable tail-calls are left to
 * the owning entity (one-line owner comments at the call sites).
 *
 * What is modelled here (every decomp field WRITE + every RNG DRAW, in order):
 *   - FixedUpdate gate: returns while !awake(0x18); when dead(0x38) it clears
 *     awake (BossAI07__FixedUpdate). No RNG.
 *   - Scout gate: only when !dead(0x38) && !dizzy(0xa1) it clears
 *     target_obj(0x7c) (BossAI07__Scout). No RNG.
 *   - ShootReflection: if can_shoot(0x40) and !dead && !dizzy, clears
 *     can_shoot and dispatches StartAtk01 (owner anim) with NO draw; otherwise,
 *     when rg_random(0xc) is live, draws ONE float Range(scout_rate*0.5,
 *     scout_rate) for the re-Invoke delay (BossAI07__ShootReflection).
 *   - BossAngry: angry(0xb0)=1, shoot_cd(0x3c) *= 0.5, both written
 *     unconditionally on every call (no angry latch in the decomp). The anim
 *     speed 1.2 + "angry" bool are owner-side (BossAI07__BossAngry). No RNG.
 *   - OnGameStateChange: on game_state==1 with the room ready, sets awake(0x18)
 *     (BossAI07__OnGameStateChange). No RNG.
 *   - InAtk01: one int Range(0,100) (BossAI07__InAtk01).
 *   - CreateBullet1 / CreateBullet4: one int Range(0,20) each, gated behind a
 *     boss_clip(0xb8) array-length >= 3 guard (BossAI07__CreateBullet1/4).
 *   - CreateBullet2 ring size: 4 normally, 6 while angry (the decomp's
 *     8/12 -> uVar1>>1 loop count) (BossAI07__CreateBullet2). No RNG.
 *
 * Owner-only bodies (no brain logic): Start, StartAtk01, FixedRotation,
 * ChildDead, CreateTransferGate, CreateBullet3, CreateBullet5, CreateBullet6.
 *
 * @see metadata BossAI07.cs (field names: angry, boss_clip, child_elf,
 *      atk2_count, bullet01..06); offsets via RGEController.cs.
 *      FAITHFUL: BossAI07 @ game_full.c:441948-442450.
 */
class BossAI07 {
public:
    // ---- golden scalars re-derived from the decomp -------------------------

    /// shoot_cd(0x3c) multiplier applied once on enrage (BossAngry: *= 0.5).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// ShootReflection's re-Invoke delay is Range(scout_rate*0.5, scout_rate).
    static constexpr float kReinvokeDelayLowScale = 0.5F;
    /// InAtk01 roll ceiling: Range(0, 100), max EXCLUSIVE.
    static constexpr int kInAtk01RollCeiling = 100;
    /// CreateBullet1/4 roll ceiling: Range(0, 0x14) == Range(0, 20), exclusive.
    static constexpr int kCreateBulletRollCeiling = 20;
    /// CreateBullet2 ring size when calm (decomp 8 -> 8>>1 == 4 iterations).
    static constexpr int kBullet2RingCalm = 4;
    /// CreateBullet2 ring size when angry (decomp 0xc -> 12>>1 == 6 iterations).
    static constexpr int kBullet2RingAngry = 6;
    /// game_state value that opens the wake gate in OnGameStateChange.
    static constexpr int kGameStateStart = 1;
    /// boss_clip(0xb8) length the SFX-indexed CreateBullet1/4 path requires.
    static constexpr int kBossClipMinLen = 3;

    BossAI07() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- observable state (only fields the decomp writes) -------------------

    bool Awake() const { return m_Awake; }
    bool Dead() const { return m_Dead; }
    bool Dizzy() const { return m_Dizzy; }
    bool CanShoot() const { return m_CanShoot; }
    bool Angry() const { return m_Angry; }
    bool TargetCleared() const { return m_TargetCleared; }
    float ShootCd() const { return m_ShootCd; }

    /// Test/owner hooks to drive the gates the decomp reads (not RNG draws).
    void SetDead(bool dead) { m_Dead = dead; }
    void SetDizzy(bool dizzy) { m_Dizzy = dizzy; }
    void SetCanShoot(bool v) { m_CanShoot = v; }
    void SetScoutRate(float r) { m_ScoutRate = r; }
    void SetShootCd(float cd) { m_ShootCd = cd; }

    // ---- modelled bodies ----------------------------------------------------

    /**
     * @brief Per-FixedUpdate gate head (BossAI07__FixedUpdate).
     *
     * Returns false (no further tick) while !awake. When dead, clears awake and
     * returns false. Otherwise returns true (owner runs the vtable EnemyUpdate
     * tail-call at *(this+0x11c), which is not recoverable here). No RNG draw.
     * @return true iff the owner should run the per-frame update tail.
     */
    bool FixedUpdate();

    /**
     * @brief Scout gate (BossAI07__Scout). Only when !dead && !dizzy does it
     *        clear target_obj(0x7c); owner then re-reads transform. No RNG draw.
     * @return true iff target_obj was cleared this call.
     */
    bool Scout();

    /**
     * @brief Pre-shot reflection step (BossAI07__ShootReflection).
     *
     * If can_shoot(0x40) and !dead && !dizzy: clears can_shoot, the owner fires
     * StartAtk01 (anim SetTrigger), and NO RNG is drawn. Otherwise, when the
     * rg_random stream is live, draws ONE float Range(scout_rate*0.5,
     * scout_rate) used to re-Invoke ShootReflection.
     * @param[out] reInvokeDelay receives the float delay on the non-attack path
     *             (untouched on the StartAtk01 path).
     * @return true if it dispatched the attack (no draw); false if it drew the
     *         re-Invoke delay instead.
     */
    bool ShootReflection(float &reInvokeDelay);

    /**
     * @brief Enrage transition (BossAI07__BossAngry): angry(0xb0)=1 and
     *        shoot_cd(0x3c) *= 0.5. The decomp has no angry latch, so both
     *        writes run unconditionally on every call (a repeat call halves
     *        shoot_cd again). anim speed 1.2 + the "angry" bool are owner-side.
     *        No RNG draw.
     */
    void BossAngry();

    /**
     * @brief Room-start wake gate (BossAI07__OnGameStateChange). On
     *        game_state==kGameStateStart with the room ready, sets awake(0x18)
     *        and returns true (owner runs the vtable StartBossAI tail at
     *        *(this+0x104)). No RNG draw.
     * @param gameState the broadcast state.
     * @param roomReady the_maker.the_room ready flag (offset chain 0x94->0x28->0x10==1).
     * @return true iff awake was just set (owner should start the boss AI).
     */
    bool OnGameStateChange(int gameState, bool roomReady);

    /**
     * @brief Atk01 in-frame tick (BossAI07__InAtk01): one int Range(0, 100).
     *        The decomp body writes no state field (the roll feeds an owner-side
     *        spawn/branch that is tail-called and not recoverable). Draw only.
     * @return the [0,100) roll.
     */
    int InAtk01();

    /**
     * @brief CreateBullet1 / CreateBullet4 roll (BossAI07__CreateBullet1/4):
     *        one int Range(0, 20). The boss_clip PlayEffect and the bullet spawn
     *        are owner-side; this is the single shared RNG draw. The decomp gates
     *        the body behind boss_clip.Length >= 3 (kBossClipMinLen); callers
     *        that fail that guard take NO draw (stream lockstep).
     * @return the [0,20) roll.
     */
    int CreateBulletRoll();

    /**
     * @brief CreateBullet2 ring count (BossAI07__CreateBullet2): 8 calm / 12
     *        angry, halved by the decomp's uVar1>>1 into the spawn loop count
     *        (4 calm / 6 angry). Pure scalar, no RNG, no field write.
     * @return iteration count for the bullet ring.
     */
    int CreateBullet2RingCount() const;

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    // Fields the decomp actually writes / gates on (decoded via RGEController.cs).
    bool m_Awake = false;       ///< awake (0x18)
    bool m_Dead = false;        ///< dead (0x38)
    bool m_Dizzy = false;       ///< dizzy (0xa1)
    bool m_CanShoot = false;    ///< can_shoot (0x40)
    bool m_Angry = false;       ///< angry (0xb0)
    bool m_TargetCleared = false; ///< latched view of target_obj(0x7c)=null
    float m_ScoutRate = 0.0F;   ///< scout_rate (0x98) - owner-provided cadence
    float m_ShootCd = 0.0F;     ///< shoot_cd (0x3c) - halved on enrage
};

} // namespace Game

#endif /* GAME_BOSS_AI07_HPP */
