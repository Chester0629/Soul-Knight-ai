#ifndef GAME_BOSS_AI10_HPP
#define GAME_BOSS_AI10_HPP

#include "data/RGRandom.hpp"

#include <glm/glm.hpp>

namespace Game {

/**
 * @class BossAI10
 * @brief Faithful logic brain for the ice boss (RGEController subclass, BossAI10).
 *
 * Per-content port: a boss-specific logic layer over the deterministic RGRandom
 * stream. Models only the pure, unit-testable brain; sprite swaps, animator
 * triggers/bools, bullet/icicle spawns, Invoke()/coroutine chains, rigidbody and
 * transform reads are all left to the owning entity (named in one-line comments).
 *
 * Field offsets (decoded against recreation/Enemy/RGEController.cs + the BossAI10
 * IL2CPP field dump):
 *   - 0x18 awake (byte)        - 0x38 dead (byte)        - 0xA1 dizzy (byte)
 *   - 0x40 can_shoot (byte)    - 0x0C rg_random          - 0x5C anim
 *   - 0x7C target_obj          - 0x3C shoot_cd (float)   - 0x70 role_attribute
 *     (role_attribute.hp @ +0x1C, max_hp @ +0x18)
 *   - 0xCC angry (byte, BossAI10-specific; CreateIcicle reads it as param_1[0x33]
 *     to choose the icicle cap 4 vs 3; also read by GetHurt/InAtk01/InAtk02)
 *   - 0xD0 atk_4_count (int, BossAI10-specific; CreateIcicle chain counter,
 *     read/written as param_1[0x34]; InAtk04 zeroes byte 0xD0)
 *
 * Modelled pure-logic surface:
 *   - RunReflection: TWO float Range(-1f, 1f) draws -> random move direction
 *     (x, y) normalized; sets move_direction (animator "run" bool is owner-side).
 *   - ShootReflection: the ONE int Range(0, 100) attack-selection draw, gated by
 *     can_shoot && !dead && !dizzy (gated-out path takes NO draw - stream lockstep).
 *     The roll->StartAtkNN jumptable could not be recovered (indirect jump at
 *     0x00ad05f4, "Too many branches"); the 5 equal buckets are a reconstruction.
 *   - GetHurt -> BossAngry transition at hp/max_hp < 0.5, once (field 0xCC).
 *   - BossAngry: angry=1; shoot_cd(0x3C) *= 0.5 (faster cadence). animator
 *     set_speed(1.2)/SetBool are owner-side.
 *   - CreateIcicle chain: atk_4_count(0xD0) increments; cap is 3, or 4 when
 *     angry(0xCC) is set; under the cap re-Invoke after (count*0.25)s, else end.
 *   - Scout / Dizzy / OnGameStateChange gates (dizzy/dead/awake latches).
 *
 * @see recreation RGEController.cs; FAITHFUL: BossAI10 @ game_full.c:955880-956657.
 */
class BossAI10 {
public:
    /// hp/max_hp below this fraction triggers the angry phase (GetHurt->BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// Number of distinct attacks: Atk01..Atk05 (returned bucket in [1, kAttackCount]).
    static constexpr int kAttackCount = 5;
    /// Idle / no-attack sentinel returned by ShootReflection when gated out.
    static constexpr int kNoAttack = 0;
    /// ShootReflection attack-selection roll ceiling (Range(0, 100), max exclusive).
    static constexpr int kRollCeiling = 100;
    /// RunReflection random-direction component bounds (Range(-1f, 1f), max inclusive).
    static constexpr float kDirMin = -1.0F;
    static constexpr float kDirMax = 1.0F;
    /// BossAngry multiplies shoot_cd (0x3C) by this (twice as fast in phase 2).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// CreateIcicle re-Invoke spacing scalar: delay = atk_4_count * this (seconds).
    static constexpr float kIcicleInvokeStep = 0.25F;
    /// CreateIcicle chain length cap when calm (angry 0xCC clear).
    static constexpr int kIcicleCap = 3;
    /// CreateIcicle chain length cap when enraged (angry 0xCC set).
    static constexpr int kIcicleCapHit = 4;

    BossAI10() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- gate state (decoded field latches) ---------------------------------
    void SetAwake(bool v) { m_Awake = v; }   ///< 0x18 awake
    void SetDead(bool v) { m_Dead = v; }     ///< 0x38 dead
    void SetDizzy(bool v) { m_Dizzy = v; }   ///< 0xA1 dizzy
    void SetCanShoot(bool v) { m_CanShoot = v; } ///< 0x40 can_shoot
    /// Force the angry phase (0xCC); CreateIcicle uses it to pick the icicle cap.
    void SetAngry(bool v) { m_Angry = v; }

    bool Awake() const { return m_Awake; }
    bool Dead() const { return m_Dead; }
    bool Dizzy() const { return m_Dizzy; }
    bool Angry() const { return m_Angry; }       ///< 0xCC angry
    bool CanShoot() const { return m_CanShoot; }
    int Atk4Count() const { return m_Atk4Count; }///< 0xD0 atk_4_count

    /**
     * @brief Pick a random unit move direction (RunReflection's two draws).
     *
     * FAITHFUL: BossAI10__RunReflection @ game_full.c:955988. Draws Range(-1f, 1f)
     * for x then Range(-1f, 1f) for y (float, max inclusive), in that exact order,
     * and returns the normalized vector (the original's set_move_direction(value)).
     * The (0,0) degenerate case returns (0,0). Animator "run" SetBool is owner-side.
     */
    glm::vec2 RunReflectionDirection();

    /**
     * @brief ShootReflection attack-selection roll (the ONE int draw), gated.
     *
     * FAITHFUL: BossAI10__ShootReflection @ game_full.c:956046. The draw
     * Range(0, 100) happens ONLY when can_shoot && !dead && !dizzy; the gated-out
     * path takes NO draw (stream lockstep). The decomp writes NO field here: it
     * reads the gates, takes the one draw, then dispatches via the vtable jumptable
     * to StartAtkNN. This method does not store the result (BossAI10 has no
     * atk_index state). When gated out, returns kNoAttack.
     *
     * The roll->StartAtkNN dispatch (indirect jump 0x00ad05f4, jumptable not
     * recovered) is reconstructed as 5 equal buckets; the single Range(0,100) draw
     * keeps the stream in lockstep regardless of bucketing.
     * @return the reconstructed bucket in [1, kAttackCount], or kNoAttack if gated
     *         out. The value is returned only; no field is set.
     */
    int ShootReflectionChooseAttack();

    /**
     * @brief Apply a hit's resulting HP; enters angry once at < 50% (GetHurt path).
     *
     * FAITHFUL: BossAI10__GetHurt -> BossAngry @ game_full.c:956221/956294. The
     * decomp gate is awake(0x18) && !dead(0x38); after RGEController::GetHurt
     * applies the damage, the angry transition fires when hp/max_hp < 0.5 and not
     * already angry (0xCC). On entering angry, BossAngry sets angry=1 and halves
     * shoot_cd (0x3C). NOTE: the decomp's heal+clamp (hp += 4 then clamp to max_hp)
     * is NOT unconditional - it lives inside the Animator.GetBool("atk03")==1
     * branch and is owner-side animator-gated state the brain does not model; the
     * ratio test below uses the post-damage hp/max_hp directly.
     * @param hpAfter   role_attribute.hp after the damage delta (field +0x1C).
     * @param maxHp     role_attribute.max_hp (field +0x18).
     * @param shootCd   current shoot_cd (0x3C) in; updated when angry fires.
     * @return resulting shoot_cd after a possible BossAngry halving.
     */
    float OnHurt(int hpAfter, int maxHp, float shootCd);

    /**
     * @brief Drive one step of the CreateIcicle Invoke chain (InAtk04 spawn loop).
     *
     * FAITHFUL: BossAI10__CreateIcicle @ game_full.c:956547. Increments
     * atk_4_count (byte 0xD0, decompiled as param_1[0x34]); the chain cap is
     * kIcicleCap, or kIcicleCapHit when angry (byte 0xCC, decompiled as the
     * (char)param_1[0x33] read - the same field BossAngry writes and
     * GetHurt/InAtk01/InAtk02 read). While atk_4_count < cap the original
     * re-Invokes "CreateIcicle" after atk_4_count * 0.25s (returned here for the
     * owner to schedule); at/after the cap it ends the attack (vtable end-attack
     * slot). Icicle spawn + RGMusicManager PlayEffect are owner-side.
     * @return seconds until the next CreateIcicle Invoke, or < 0 when the chain ends.
     */
    float CreateIcicleStep();

    /// InAtk04 entry: zeroes the icicle chain counter (byte 0xD0) + move_dir=0.
    /// FAITHFUL: BossAI10__InAtk04 @ game_full.c:956523 sets move_direction to zero
    /// (owner-side) and zeroes byte 0xD0; the brain resets atk_4_count so
    /// CreateIcicleStep starts a fresh chain.
    void StartIcicleChain() { m_Atk4Count = kNoAttack; }

    /**
     * @brief Dizzy(): latch dizzy when not dead (Dizzy gate).
     * FAITHFUL: BossAI10__Dizzy @ game_full.c:956625. Only when !dead(0x38) set
     * dizzy(0xA1)=1; animator "run" SetBool(false) is owner-side.
     * @return true if dizzy was newly latched.
     */
    bool ApplyDizzy();

    /**
     * @brief OnGameStateChange: wake the boss when the room reports it started.
     * FAITHFUL: BossAI10__OnGameStateChange @ game_full.c:956420. Only when
     * gameState==1 and the room-ready flag is set does it set awake(0x18)=1;
     * animator atk03 bool SetBool(false) is owner-side.
     * @param gameState   the broadcast state (1 == room start).
     * @param roomReady   the_maker.the_room ready flag (decomp reads ==1).
     * @return true if awake was newly set.
     */
    bool OnGameStateChange(int gameState, bool roomReady);

    /**
     * @brief Scout(): true when the boss may run its scout step this tick.
     * FAITHFUL: BossAI10__Scout @ game_full.c:955963. Gated out (returns false,
     * no state change) while dizzy(0xA1) or dead(0x38); when clear the original
     * clears target_obj(0x7C) and reads transform (owner-side).
     */
    bool ScoutActive() const { return !m_Dizzy && !m_Dead; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Awake = false;     ///< 0x18 awake
    bool m_Dead = false;      ///< 0x38 dead
    bool m_Dizzy = false;     ///< 0xA1 dizzy
    bool m_CanShoot = false;  ///< 0x40 can_shoot
    bool m_Angry = false;     ///< 0xCC angry (also gates CreateIcicle cap 4 vs 3)
    int m_Atk4Count = kNoAttack; ///< 0xD0 atk_4_count
};

} // namespace Game

#endif /* GAME_BOSS_AI10_HPP */
