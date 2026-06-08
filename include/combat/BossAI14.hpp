#ifndef GAME_BOSS_AI14_HPP
#define GAME_BOSS_AI14_HPP

#include "data/RGRandom.hpp"

#include <glm/glm.hpp>

namespace Game {

/**
 * @class BossAI14
 * @brief Faithful movement/reflection/attack-cadence brain for boss #14
 *        (RGEController subclass; the "octopus / many-hand" boss).
 *
 * Per-content port. Models only the pure, deterministic logic over the
 * per-instance RGRandom stream; everything owner-side (Animator triggers/bools,
 * Rigidbody2D velocity, bullet Instantiate, RGMusicManager effects, BossInfo HP
 * bar, transfer-gate spawn, the Atk02 coroutine iterators) is left to the owning
 * entity and only named in comments. Modelled here, in decomp order:
 *   - the ctor seed of the two latch flags root/can_hit (ctor writes 0x101 over
 *     fields 0xe5/0xe6);
 *   - RunReflection: the move-direction reroll. Two modes gated on `root`:
 *       * root: schedule the next reflection after Range(scout_rate*0.5,
 *         scout_rate) seconds (ONE float draw), no direction change;
 *       * not root: pick a random unit-ish heading via TWO Range(-1f,1f) float
 *         draws (x then y), normalize, set move_direction, raise the walk bool.
 *   - ShootReflection: the attack-selection roll. Gated on can_shoot(0x40) &&
 *     !dead(0x38) && !dizzy(0xa1): ONE Range(0,100) int draw; the roll->StartAtkNN
 *     jumptable could not be recovered (indirect jump at 0x00adfc50), so the
 *     bucketing into Atk01..Atk06 is a reconstruction (the single draw keeps the
 *     stream in lockstep regardless of bucketing).
 *   - BossAngry: the one-shot enrage (sets angry=1 at 0xe4, halves shoot_cd 0x3c,
 *     animator speed 1.2 + "angry" bool).
 *   - Scout / Dizzy / GetForce gating (dead/dizzy/root/inAtk03 latches) with NO
 *     RNG draws (stream lockstep).
 *
 * Field offsets decoded against recreation/Enemy/RGEController.cs (base) and the
 * BossAI14 metadata field order (boss_info 0xac, boss_clip 0xb0, bullet01 0xb4..
 * bullet06 0xc8, h1 0xcc, h2 0xd0, h3 0xd4, energy_ball 0xd8, star 0xdc, body
 * 0xe0, angry 0xe4, root 0xe5, can_hit 0xe6, atk_4_count 0xe8, bulletStay 0xec,
 * inAtk03 0xed).
 *
 * @see BossAI14.cs (metadata: names/types only); FAITHFUL bodies:
 *      game_full.c:958662-959620.
 */
class BossAI14 {
public:
    /// Number of distinct attacks Atk01..Atk06 (StartAtk01..StartAtk06 exist).
    static constexpr int kAttackCount = 6;
    /// Idle / no-attack sentinel for the modelled attack index.
    static constexpr int kNoAttack = 0;
    /// ShootReflection roll ceiling: Range(0, 100), max EXCLUSIVE.
    static constexpr int kRollCeiling = 100;
    /// RunReflection random-heading component bounds: Range(-1f, 1f), max INCLUSIVE.
    static constexpr float kHeadingMin = -1.0F;
    static constexpr float kHeadingMax = 1.0F;
    /// RunReflection root-mode reschedule delay = Range(scout_rate*0.5, scout_rate).
    static constexpr float kRootDelayLoFactor = 0.5F;
    /// BossAngry: shoot_cd (0x3c) is multiplied by this (halved) on enrage.
    static constexpr float kAngryShootCdScale = 0.5F;
    /// BossAngry: animator playback speed pushed to this (0x3f99999a == 1.2f).
    static constexpr float kAngryAnimSpeed = 1.2F;

    BossAI14() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- base RGEController latches the brain reads/writes -------------------
    // These mirror the byte fields the decomp gates on; the owner keeps them in
    // sync with the live entity before calling the brain.
    bool Dead() const { return m_Dead; }
    void SetDead(bool v) { m_Dead = v; }
    bool Dizzy() const { return m_Dizzy; }
    bool CanShoot() const { return m_CanShoot; }
    void SetCanShoot(bool v) { m_CanShoot = v; }

    // ---- BossAI14-specific latches ------------------------------------------
    /// 0xe5 root: when set, RunReflection only reschedules (no move reroll).
    bool Root() const { return m_Root; }
    void SetRoot(bool v) { m_Root = v; }
    /// 0xe6 can_hit (ctor-seeded with root via the 0x101 word write).
    bool CanHit() const { return m_CanHit; }
    /// 0xed inAtk03: one of the GetForce immunity gates.
    bool InAtk03() const { return m_InAtk03; }
    void SetInAtk03(bool v) { m_InAtk03 = v; }
    /// 0xe4 angry: enrage latch (set once by BossAngry).
    bool Angry() const { return m_Angry; }

    /**
     * @brief BossAI14.__ctor field seed: writes 0x101 over (root@0xe5, can_hit@0xe6).
     * FAITHFUL: BossAI14___ctor @ game_full.c:958662 (*(u16*)(p+0xe5)=0x101).
     * Little-endian: low byte -> root=1, high byte -> can_hit=1.
     */
    void Construct();

    /**
     * @brief Scout() gate: clears target (0x7c) only when NOT dizzy and NOT dead.
     * FAITHFUL: BossAI14__Scout @ game_full.c:958811. No RNG draw.
     * @return true when the (modelled) target-clear path is taken.
     */
    bool ScoutClearsTarget() const;

    /**
     * @brief RunReflection in `root` mode: schedule the next reflection.
     * FAITHFUL: BossAI14__RunReflection @ game_full.c:958838 (root branch).
     * Draws ONE float Range(scout_rate*0.5, scout_rate). Returns that delay; the
     * owner passes it to Invoke("RunReflection", delay). No direction change.
     */
    float RunReflectionRootDelay(float scoutRate);

    /**
     * @brief RunReflection non-root mode: pick a random heading and steer.
     * FAITHFUL: BossAI14__RunReflection @ game_full.c:958838 (else branch).
     * Draws TWO floats Range(-1f,1f) (x then y, in that order), then normalizes.
     * The owner sets move_direction to the result and raises the walk anim bool.
     * @return the normalized heading (zero vector if both draws were 0).
     */
    glm::vec2 RunReflectionHeading();

    /**
     * @brief ShootReflection attack-selection roll.
     * FAITHFUL: BossAI14__ShootReflection @ game_full.c:958947.
     * Gated on can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1). When the gate is
     * open, draws ONE Range(0,100) int and (reconstruction) buckets it into
     * Atk01..Atk06, returning the bucket as a PURE VALUE. The decomp dispatches
     * the roll through the unrecovered vtable[0x10c] jumptable (indirect jump at
     * 0x00adfc50) and writes NO index field; BossAI14 has no atk_index member, so
     * nothing is stored here (rule #2). When gated out, draws NOTHING (stream
     * lockstep) and returns kNoAttack.
     * @return chosen attack index in [1, kAttackCount], or kNoAttack if gated.
     */
    int ShootReflectionChoose();

    /**
     * @brief BossAngry: the one-shot enrage transition.
     * FAITHFUL: BossAI14__BossAngry @ game_full.c:959191. Sets angry(0xe4)=1 and
     * halves shoot_cd(0x3c); owner pushes animator speed 1.2 + "angry" bool.
     * Applies once; the second call is a no-op (matches the latch). No RNG draw.
     * @param shootCd current shoot_cd; @return the halved shoot_cd (unchanged if
     *        already angry).
     */
    float BossAngry(float shootCd);

    /**
     * @brief Dizzy(): latch dizzy only when not dead (drops the walk anim).
     * FAITHFUL: BossAI14__Dizzy @ game_full.c:959472 (gate *(p+0x38)==0). The
     * second call is a no-op via the dead/dizzy guard. No RNG draw.
     * @return true when the dizzy latch was newly set this call.
     */
    bool ApplyDizzy();

    /**
     * @brief EndAtk03: lower the atk03 animation bool (owner-side animator).
     * FAITHFUL: BossAI14__EndAtk03 @ game_full.c:959455. The decomp ONLY calls
     * Animator.SetBool("atk03", false) -- it does NOT write field 0xed. The
     * inAtk03(0xed) latch is owned by the InAtk03/Attacking03 coroutine iterators
     * (set=1 at coroutine start, cleared at coroutine end), NOT by EndAtk03, so
     * this is a pure owner-side no-op stub here. No RNG draw.
     */
    void EndAtk03();

    /**
     * @brief GetForce immunity gate.
     * FAITHFUL: BossAI14__GetForce @ game_full.c:959519: base GetForce runs only
     * when inAtk03(0xed)==0 AND root(0xe5)==0. No RNG draw.
     * @return true when the boss may receive knockback (base GetForce would run).
     */
    bool AcceptsForce() const;

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    // base RGEController latches the brain gates on
    bool m_Dead = false;     // 0x38
    bool m_Dizzy = false;    // 0xa1
    bool m_CanShoot = false; // 0x40

    // BossAI14-specific latches
    bool m_Root = false;    // 0xe5
    bool m_CanHit = false;  // 0xe6
    bool m_Angry = false;   // 0xe4
    bool m_InAtk03 = false; // 0xed
};

} // namespace Game

#endif /* GAME_BOSS_AI14_HPP */
