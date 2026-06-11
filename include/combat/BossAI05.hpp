#ifndef GAME_BOSS_AI05_HPP
#define GAME_BOSS_AI05_HPP

#include "data/RGRandom.hpp"

#include <glm/glm.hpp>

namespace Game {

/**
 * @class BossAI05
 * @brief Faithful phase/cadence/attack-selection brain for the fifth boss
 *        (the tentacle/invisible boss; RGEController subclass BossAI05).
 *
 * Per-content port: a boss-specific logic layer over the deterministic
 * RGRandom stream. Models only the pure, unit-testable brain; sprite-renderer
 * colour swaps, animator triggers, bullet/laser spawns, music PlayEffect and
 * Invoke()/coroutine chains are left to the owning entity. Modelled here:
 *   - Scout's idle gate (dead 0x38 / dizzy 0xA1) -> target_obj = null;
 *   - RunReflection's wander roll: two Range(-1f, 1f) draws normalised into
 *     move_direction (BossAI05.RunReflection);
 *   - ShootReflection's attack-selection gate + roll: can_shoot 0x40 then
 *     dead 0x38 == 0 && dizzy 0xA1 == 0 -> a single Range(0, 100) before the
 *     (unrecovered) StartAtkNN jumptable;
 *   - InAtk01's per-shot Range(0, 100) draw (BossAI05.InAtk01);
 *   - the single angry-phase transition (GetHurt -> BossAngry) at
 *     hp/max_hp < 0.5, gated by angry 0xB0 == 0, which also HALVES shoot_cd
 *     (0x3C) -- BossAI05.BossAngry, distinct from BossAI04 which leaves cadence
 *     to StartAtk02;
 *   - the GetHurt damage gate: awake 0x18 && dead 0x38 == 0 && invisible
 *     0xD0 == 0;
 *   - Dizzy (0xA1 latch, gated by dead 0x38 == 0);
 *   - TurnInvisible / BackInvisible (invisible 0xD0 latch);
 *   - StartAtk03's weapon_lock_target (0x1C) clear.
 *
 * Field offsets decoded against RGEController.cs + BossAI05.cs:
 *   0x0C rg_random, 0x18 awake, 0x1C weapon_lock_target, 0x38 dead,
 *   0x3C shoot_cd, 0x40 can_shoot, 0x70 role_attribute (+0x18 max_hp,
 *   +0x1C hp), 0x7C target_obj, 0x80 shooting, 0xA1 dizzy, 0xB0 angry,
 *   0xD0 invisible.
 *
 * @see recreation BossAI05.cs; FAITHFUL: BossAI05 @ game_full.c:439845-440600
 *      (boss05; RoleAttribute hp/max_hp at role_attribute+0x1C / +0x18).
 */
class BossAI05 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// BossAngry multiplies shoot_cd (0x3C) by this (faster firing in phase 2).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// BossAngry sets animator speed to this (owner-side anim.set_speed(1.2f)).
    static constexpr float kAngryAnimSpeed = 1.2F;

    /// RunReflection wander component bounds: Range(-1f, 1f) (max INCLUSIVE).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;

    /// ShootReflection / InAtk01 roll ceiling: Range(0, 100) (max EXCLUSIVE).
    static constexpr int kRollCeiling = 100;

    BossAI05() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // -- gate inputs (owner pushes these each frame; brain only reads them) ---
    void SetAwake(bool v) { m_Awake = v; }
    void SetDead(bool v) { m_Dead = v; }
    bool Awake() const { return m_Awake; }
    bool Dead() const { return m_Dead; }

    // -- latched state the decomp actually writes -----------------------------
    bool Angry() const { return m_Angry; }       // 0xB0
    bool Dizzy() const { return m_Dizzy; }        // 0xA1
    bool Invisible() const { return m_Invisible; }// 0xD0
    bool CanShoot() const { return m_CanShoot; }  // 0x40 (gate input)
    void SetCanShoot(bool v) { m_CanShoot = v; }
    bool WeaponLockTarget() const { return m_WeaponLockTarget; } // 0x1C
    bool HasTarget() const { return m_HasTarget; } // 0x7C != null
    void SetHasTarget(bool v) { m_HasTarget = v; }

    /// shoot_cd (0x3C); BossAngry halves it once. Seed the original cadence here.
    float ShootCd() const { return m_ShootCd; }
    void SetShootCd(float v) { m_ShootCd = v; }

    /**
     * @brief Scout idle tick: gated by dead 0x38 == 0 && dizzy 0xA1 == 0.
     *
     * FAITHFUL: BossAI05__Scout @ game_full.c:439938. When neither dead nor
     * dizzy, the decomp clears target_obj (0x7C) and re-runs the chase via
     * get_transform (owner). Returns true when the (owner) re-scout runs.
     * Draws NO RNG.
     */
    bool Scout();

    /**
     * @brief RunReflection wander: two Range(-1f, 1f) draws -> move_direction.
     *
     * FAITHFUL: BossAI05__RunReflection @ game_full.c:439963. Draws x then y
     * (float, max INCLUSIVE) and normalises; owner does Animator.SetBool. The
     * normalisation here mirrors the original FUN_00fa1e04 (Vector2.normalized).
     * @return unit (or zero) wander direction.
     */
    glm::vec2 WanderDirection();

    /**
     * @brief ShootReflection attack-selection gate + roll.
     *
     * FAITHFUL: BossAI05__ShootReflection @ game_full.c:440021. Gate order:
     * can_shoot (0x40) != 0, THEN dead (0x38) == 0 && dizzy (0xA1) == 0. Only
     * when both pass does the decomp draw a single Range(0, 100) (the roll fed
     * to the unrecovered StartAtkNN jumptable at 0x547360). A gated-out path
     * draws NOTHING (stream lockstep).
     * @param rolledOut receives the roll when it fires; left untouched when gated.
     * @return true if the roll fired (a draw was consumed), false if gated out.
     */
    bool ShootReflection(int &rolledOut);

    /**
     * @brief InAtk01 per-shot roll: a single Range(0, 100).
     *
     * FAITHFUL: BossAI05__InAtk01 @ game_full.c:440357. The decomp body is a
     * tail-call straight into the roll then a bullet-spawn jumptable; it writes
     * no brain field. Draws one int.
     * @return the roll in [0, 100).
     */
    int InAtk01Roll();

    /**
     * @brief StartAtk03: clears weapon_lock_target (0x1C).
     *
     * FAITHFUL: BossAI05__StartAtk03 @ game_full.c:440104. The only brain field
     * the body writes is weapon_lock_target = 0 (the rest is move_direction =
     * zero, animator SetTrigger and music PlayEffect -- owner). No RNG.
     */
    void StartAtk03();

    /**
     * @brief GetHurt: damage gate + one-shot angry transition.
     *
     * FAITHFUL: BossAI05__GetHurt @ game_full.c:440228. Gate (returns, no
     * effect) unless awake (0x18) && dead (0x38) == 0 && invisible (0xD0) == 0.
     * On a real hit, when hp/max_hp < 0.5 and angry (0xB0) == 0, calls
     * BossAngry. (RGEController.GetHurt and BossInfo.UpDateBossHp are owner.)
     * @return true if the hit was accepted (gate passed).
     */
    bool OnHurt(int hpAfter, int maxHp);

    /**
     * @brief BossAngry: phase-2 latch + cadence change.
     *
     * FAITHFUL: BossAI05__BossAngry @ game_full.c:440277. Sets angry (0xB0) = 1
     * and HALVES shoot_cd (0x3C *= 0.5). (anim.set_speed(1.2f) and
     * anim.SetBool("angry", true) are owner.) Idempotent via the GetHurt gate;
     * callable directly for tests.
     */
    void BossAngry();

    /**
     * @brief Dizzy: stun latch, gated by dead 0x38 == 0.
     *
     * FAITHFUL: BossAI05__Dizzy @ game_full.c:440574. When not dead, sets dizzy
     * (0xA1) = 1 (anim.SetBool to drop the move state is owner). No RNG.
     * @return true if the stun latched.
     */
    bool Dizzy(float value1);

    /**
     * @brief TurnInvisible: invisibility latch, gated by invisible 0xD0 == 0.
     *
     * FAITHFUL: BossAI05__TurnInvisible @ game_full.c:440515. When not already
     * invisible, sets invisible (0xD0) = 1 (sprite colour alpha 0.5 and
     * Invoke("BackInvisible", 2f) are owner). No RNG.
     * @return true if it latched this call.
     */
    bool TurnInvisible();

    /**
     * @brief BackInvisible: clears invisibility (0xD0) = 0.
     *
     * FAITHFUL: BossAI05__BackInvisible @ game_full.c:440548. Sets invisible = 0
     * (sprite colour alpha back to 1.0 is owner). No gate, no RNG.
     */
    void BackInvisible();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    // Gate inputs the owner mirrors in (the decomp reads these as fields):
    bool m_Awake = false;   // 0x18
    bool m_Dead = false;    // 0x38
    bool m_CanShoot = false;// 0x40
    bool m_HasTarget = false;// 0x7C != null

    // Latched state the decomp writes:
    bool m_Angry = false;          // 0xB0
    bool m_Dizzy = false;          // 0xA1
    bool m_Invisible = false;      // 0xD0
    bool m_WeaponLockTarget = false;// 0x1C (StartAtk03 clears it)

    float m_ShootCd = 0.0F; // 0x3C (BossAngry halves)
};

} // namespace Game

#endif /* GAME_BOSS_AI05_HPP */
