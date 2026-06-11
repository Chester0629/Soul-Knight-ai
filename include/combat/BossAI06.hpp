#ifndef GAME_BOSS_AI06_HPP
#define GAME_BOSS_AI06_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI06
 * @brief Faithful phase/cadence brain for the "ball" boss (RGEController subclass).
 *
 * Per-content port: a boss-specific logic layer over the deterministic
 * RGEController/RGRandom base. Models only the pure, unit-testable brain; the
 * audio (RGMusicManager.PlayEffect), animator triggers/SetBool, child-ball and
 * bullet spawns (Instantiate<RGWeapon>/BossAI06Child.Atk*), transfer-gate VFX,
 * Rigidbody2D velocity, Transform reads and Invoke() chains are owning-entity
 * concerns and are left out. Modelled here, all FAITHFUL to the decomp at
 * game_full.c:440595-441620:
 *   - the wander direction (RunReflection: Range(-1,1) x2, normalized into
 *     move_direction);
 *   - the deterministic attack-selection roll (ShootReflection's Range(0,100)),
 *     fired only when the can_shoot/!dead/!dizzy gates pass (gated-out paths take
 *     NO draw, keeping the stream in lockstep);
 *   - the single angry-phase transition at hp/max_hp < 0.5 (GetHurt -> BossAngry,
 *     once): sets angry(0xb0) and (owner) animator speed 1.5 + "angry" bool;
 *   - the Atk3 fan-bullet angle roll (Atk3CreateBullet: Range(0, 360));
 *   - the InAtk02 sub-phase state machine (atk2_value switch 1..4): each of
 *     cases 1/2/4 draws one Range(0,100) and sets atk2_invoke_time(0xec) to
 *     +/-1.0; case 3 and the default take NO draw and write nothing;
 *   - the per-attack field writes the decomp actually performs
 *     (StartAtk03 -> atk3_shooting(0xf0)=1; InAtk02 -> atk2_shooting(0xf3)=1;
 *     EndAtk02 -> atk2_shooting(0xf3)=0, ball_group_rotation(0xf1)=0;
 *     BossAngry -> angry(0xb0)=1).
 *
 * Owner-only bodies (no brain logic, named for traceability): Start,
 * FixedUpdate, Scout (gated no-op), FixedRotation, CreateTransferGate, ChildDead,
 * Atk1, Atk2, Atk3 (tail), StartAtk01, StartAtk02, EndAtk02 (audio/transform),
 * ChildsCreateBullet0/1/2, Child1CreateBullet, Child3CreateBullet, Dizzy
 * (latch + owner SetBool).
 *
 * @see skeleton BossAI06.cs; offsets recreation/Enemy/RGEController.cs.
 *      FAITHFUL: BossAI06 @ game_full.c:440595-441620.
 */
class BossAI06 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (GetHurt->BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// Roll ceiling used by ShootReflection (Range(0, 100), max exclusive).
    static constexpr int kRollCeiling = 100;
    /// RunReflection per-axis wander bounds (Range(-1f, 1f), max INCLUSIVE).
    static constexpr float kWanderMin = -1.0F;
    static constexpr float kWanderMax = 1.0F;
    /// Atk3CreateBullet fan angle ceiling (Range(0, 0x168) == Range(0, 360)).
    static constexpr int kAtk3AngleCeiling = 360;
    /// InAtk02 per-case follow-up roll ceiling (Range(0, 100), max exclusive).
    static constexpr int kInAtk02RollCeiling = 100;
    /// atk2_invoke_time magnitude written by the InAtk02 sub-phases (+/-1.0f).
    static constexpr float kAtk2InvokeTime = 1.0F;
    /// Idle / no-active-sub-phase sentinel for atk2_value (0xe8).
    static constexpr int kAtk2Idle = 0;

    BossAI06() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- gate state (decoded against the RGEController offset table) ----------
    /// dead latch (0x38): gates Scout / GetHurt / Dizzy.
    void SetDead(bool dead) { m_Dead = dead; }
    bool Dead() const { return m_Dead; }
    /// dizzy latch (0xa1): gates Scout / ShootReflection.
    void SetDizzy(bool v) { m_Dizzy = v; }
    bool Dizzy() const { return m_Dizzy; }
    /// can_shoot gate (0x40): ShootReflection only rolls when set.
    void SetCanShoot(bool v) { m_CanShoot = v; }
    bool CanShoot() const { return m_CanShoot; }

    // ---- modelled per-attack field writes ------------------------------------
    bool Angry() const { return m_Angry; }
    bool Atk3Shooting() const { return m_Atk3Shooting; }   // 0xf0
    bool Atk2Shooting() const { return m_Atk2Shooting; }   // 0xf3
    bool BallGroupRotation() const { return m_BallGroupRotation; } // 0xf1
    float Atk2InvokeTime() const { return m_Atk2InvokeTime; }      // 0xec
    int Atk2Value() const { return m_Atk2Value; }                  // 0xe8

    /**
     * @brief Apply a hit's resulting HP; enters the angry phase once at < 50%.
     *
     * FAITHFUL: BossAI06__GetHurt @ game_full.c:441004. Gates: returns if not
     * awake (0x18) or already dead (0x38). The HP ratio is read from
     * role_attribute as hp(+0x1c)/max_hp(+0x18); when < 0.5 and not yet angry
     * (0xb0 == 0) it calls BossAngry. RGEController.GetHurt (HP subtraction,
     * damage number, BossInfo.UpDateBossHp) is owner-side; only the BossAngry
     * trigger + its field write are modelled here.
     * @param awake  current awake flag (0x18); a hit before awake is ignored.
     */
    void OnHurt(int hpAfter, int maxHp, bool awake);

    /**
     * @brief BossAngry effect (once): sets angry(0xb0) = 1.
     *
     * FAITHFUL: BossAI06__BossAngry @ game_full.c:441046 (field 0xb0 = 1; owner:
     * anim.set_speed(1.5f / 0x3fc00000), anim.SetBool("angry", true)). Exposed so
     * tests can drive the latch without an HP draw; OnHurt is the in-game trigger.
     */
    void BossAngry();

    /**
     * @brief Random wander direction (RunReflection: Range(-1,1) x2, normalized).
     *
     * FAITHFUL: BossAI06__RunReflection @ game_full.c:440783 (two
     * Range(-1.0f, 1.0f) draws -> Vector2 -> normalized into move_direction;
     * owner then anim.SetBool("walk", true)). Draws exactly two floats in order.
     */
    glm::vec2 WanderDirection();

    /**
     * @brief ShootReflection attack-selection roll, gated like the decomp.
     *
     * FAITHFUL: BossAI06__ShootReflection @ game_full.c:440841. The roll
     * (rg_random.Range(0, 100)) fires only when can_shoot(0x40) != 0 &&
     * !dead(0x38) && !dizzy(0xa1) && rg_random(0xc) != null. When any gate fails
     * the original takes the indirect tail (jumptable "Could not recover ... Too
     * many branches") with NO draw, so the gated-out path here consumes no RNG
     * (stream lockstep). The rg_random-null check is always satisfied for a
     * seeded brain, so it is not surfaced as a separate gate. The roll->StartAtkNN
     * dispatch was not recovered; only the single draw is modelled.
     * @return true if a roll was drawn (gate open); the roll value via @p outRoll.
     */
    bool ShootReflectionRoll(int &outRoll);

    /**
     * @brief Atk3 fan-bullet angle roll (Atk3CreateBullet: Range(0, 360)).
     *
     * FAITHFUL: BossAI06__Atk3CreateBullet @ game_full.c:441559 (single
     * Range(0, 0x168) int draw). The decomp body draws RNG only and writes no
     * field (bullet spawn is owner-side via the indirect tail).
     * @return fan angle roll in [0, kAtk3AngleCeiling).
     */
    int Atk3CreateBulletAngle();

    /**
     * @brief Begin StartAtk03: sets atk3_shooting(0xf0) = 1.
     *
     * FAITHFUL: BossAI06__StartAtk03 @ game_full.c:440941 (field 0xf0 = 1; owner:
     * move_direction = Vector2.zero, anim.SetBool("walk", false)). No RNG draw.
     */
    void StartAtk03();

    /**
     * @brief Drive one InAtk02 sub-phase (atk2_value(0xe8) switch).
     *
     * FAITHFUL: BossAI06__InAtk02 @ game_full.c:441263. The switch on atk2_value:
     *   case 1: atk2_shooting(0xf3)=1, atk2_invoke_time(0xec)=+1.0, draw Range(0,100);
     *   case 2: atk2_invoke_time(0xec)=-1.0,                       draw Range(0,100);
     *   case 3: fall-through tail (atk2_shooting(0xf3)=1, audio + child spawn,
     *           atk2_invoke_time(0xec)=-1.0) -- modelled writes only, NO draw;
     *   case 4: atk2_invoke_time(0xec)=-1.0, child bullet + Invoke chain,
     *           draw Range(0,100);
     *   default: nothing (no write, NO draw).
     * Cases 1/2/4 each consume exactly one int draw; case 3 and default consume
     * none (stream lockstep). Audio/child/Invoke side effects are owner-side.
     * @param atk2Value the sub-phase selector (0xe8), set by the owner's chain.
     * @return true if a roll was drawn; the roll value via @p outRoll.
     */
    bool InAtk02(int atk2Value, int &outRoll);

    /**
     * @brief End Atk02 (EndAtk02): atk2_shooting(0xf3)=0, ball_group_rotation(0xf1)=0.
     *
     * FAITHFUL: BossAI06__EndAtk02 @ game_full.c:441506 (writes (int)p+0xf1 = 0
     * and (int)p+0xf3 = 0; owner: PlayEffect, reset hand localEulerAngles to 0,
     * anim.SetInteger 0, Invoke chain, indirect tail). No RNG draw.
     */
    void EndAtk02();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    // RGEController gates (decoded offsets in comments).
    bool m_Dead = false;        // 0x38
    bool m_Dizzy = false;       // 0xa1
    bool m_CanShoot = false;    // 0x40

    // BossAI06-specific state the decomp actually writes.
    bool m_Angry = false;             // 0xb0
    bool m_Atk3Shooting = false;      // 0xf0
    bool m_Atk2Shooting = false;      // 0xf3
    bool m_BallGroupRotation = false; // 0xf1
    float m_Atk2InvokeTime = 0.0F;    // 0xec
    int m_Atk2Value = kAtk2Idle;      // 0xe8 (selector, owner-driven; cached on InAtk02)
};

} // namespace Game

#endif /* GAME_BOSS_AI06_HPP */
