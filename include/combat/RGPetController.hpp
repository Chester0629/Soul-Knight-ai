#ifndef GAME_RG_PET_CONTROLLER_HPP
#define GAME_RG_PET_CONTROLLER_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RGPetController
 * @brief Faithful decision/cadence/scalar base brain for friendly pets
 *        (RGBaseController -> RGPetController). This is the BASE class of the
 *        shipped WolfController / SnowmanController; those subclasses override
 *        Scout / RunReflection / EndCycle / OnAtk, but inherit the three base
 *        bodies modelled here: the FixedUpdate velocity state machine, the
 *        ReplyingHP self-heal cadence, and the TurnTo reflect.
 *
 * OFFSET MAP (decoded from the decomp's own self-consistent accesses in
 * RGPetController__* and cross-checked against recreation/Pet/RGPetController.cs;
 * decomp is truth):
 *   awake     @ 0x0C (byte)            anim      @ 0x14 (ptr, owner)
 *   rigibody  @ 0x18 (ptr, owner)      damping   @ 0x24 (float, "friction")
 *   decel     @ 0x30 (float, "inertial_vel")     force_direction @ 0x34 (Vector2)
 *   role_attribute @ 0x40 (ptr) { speed @ +0x10, speed_rate @ +0x14,
 *                                  max_hp @ +0x18 (int), hp @ +0x1C (int) }
 *   move_direction @ 0x50/0x54 (Vector2)   min_distance @ 0x58 (float = 100)
 *   reply_time1 @ 0x5C (float = 4)      reply_time2 @ 0x60 (float = 2)
 *   this_reply_time @ 0x64 (float, heal accumulator)
 *   scout_rate @ 0x68 (float = 1)       atk_cd @ 0x6C (float = 2)
 *   can_atk @ 0x71 (byte = 1)           master_tf @ 0x74 (ptr, owner)
 *   _facing @ 0x78 (int = 1)
 *
 * CTOR DEFAULTS (RGPetController___ctor @ game_full.c:427154-427166):
 *   field 0x48 = 2; move_direction(0x50/0x54) = Vector2.zero;
 *   min_distance(0x58) = 100.0; reply_time1(0x5C) = 4.0; reply_time2(0x60) = 2.0;
 *   scout_rate(0x68) = 1.0; atk_cd(0x6C) = 2.0; can_atk(0x71) = true;
 *   _facing(0x78) = 1. (this_reply_time @ 0x64 is NOT set in the ctor; the
 *   recreation arms it to reply_time1+reply_time2 in Start, whose decomp tail is
 *   truncated -> modelled as Tick-driven from 0.)
 *
 * MODELLED HERE (all pure; no Unity types):
 *   - FixedUpdate(): the two-branch velocity state machine. The branch decision
 *     (decel(0x30) <= 1.0), the follow-velocity scalar product
 *     (move_direction * speed * (speed_rate + 1.0)), the coast-velocity product
 *     (force_direction * decel), and the decel decay write-back (decel *= damping)
 *     in the coast branch. Rigidbody2D.set_velocity is the only owner write; the
 *     composed velocity vector is returned as a pure scalar result.
 *   - ReplyingHP(): the self-heal cadence. Gate hp < max_hp; accumulate the timer
 *     by deltaTime (modelled as Tick(dt)); when timer >= reply_time2 + reply_time1
 *     heal hp += max_hp / 5 (INTEGER division), reset timer = reply_time1, clamp
 *     hp = min(hp, max_hp). hp/max_hp are plain ints, fully recoverable.
 *   - TurnTo(): move_direction = Vector2.Reflect(move_direction, normal), where
 *     Reflect(v, n) = v - 2*dot(v,n)*n.
 *
 * NOT modelled (owner-side / no recoverable logic): the awake gate read is
 * modelled as a guard param; Rigidbody2D.set_velocity, Animator, get_transform,
 * GetComponent, the get_gameObject "fully healed fx" tail in ReplyingHP, and the
 * delegate/Invoke plumbing are owner concerns.
 *
 * RNG: NONE of the three base bodies draw from rg_random. The seeded stream is
 * carried only for family parity (subclass brains wrap SetSeed) and so tests can
 * assert a non-advancing stream. Modelling any draw here would be fabrication.
 *
 * @see recreation/Pet/RGPetController.cs (field cross-check);
 *      FAITHFUL: RGPetController @ game_full.c:427144-427511.
 */
class RGPetController {
public:
    // ---- ctor-default constants (RGPetController___ctor @ 427154-427166) ----

    /// min_distance (0x58): follow keep-distance, stored *100 (= 100.0).
    static constexpr float kMinDistance = 100.0F;
    /// reply_time1 (0x5C): the partial heal re-arm interval (= 4.0).
    static constexpr float kReplyTime1 = 4.0F;
    /// reply_time2 (0x60): the extra first-heal interval (= 2.0).
    static constexpr float kReplyTime2 = 2.0F;
    /// scout_rate (0x68): Scout cadence (= 1.0).
    static constexpr float kScoutRate = 1.0F;
    /// atk_cd (0x6C): attack cooldown (= 2.0).
    static constexpr float kAtkCd = 2.0F;
    /// _facing (0x78): initial facing (= 1).
    static constexpr int kInitialFacing = 1;
    /// can_atk (0x71): freshly spawned pet may attack.
    static constexpr bool kCanAtkInitial = true;
    /// Heal step divisor: hp += max_hp / 5 (line 427344, INTEGER division).
    static constexpr int kHealDivisor = 5;
    /// decel(0x30) <= this -> follow branch; otherwise coast branch (line 427270).
    static constexpr float kFollowDecelThreshold = 1.0F;

    /**
     * @brief Which FixedUpdate branch the decel threshold selects.
     *        (RGPetController__FixedUpdate @ game_full.c:427270.)
     *
     *   - Follow: decel(0x30) <= 1.0 -> velocity = move_direction * speed *
     *             (speed_rate + 1.0). decel is NOT decayed.
     *   - Coast:  decel(0x30) > 1.0 -> velocity = force_direction * decel, then
     *             decel *= damping (the only state write-back).
     */
    enum class MoveBranch {
        FOLLOW, ///< decel <= 1.0: ride move_direction at full follow speed.
        COAST   ///< decel  > 1.0: ride force_direction, decaying decel by damping.
    };

    RGPetController() = default;

    /// Seed this pet's deterministic stream (call once at spawn). No base body
    /// draws from it; carried for subclass/family parity.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----

    /// move_direction (0x50/0x54): follow steering vector.
    glm::vec2 MoveDirection() const { return m_MoveDirection; }
    void SetMoveDirection(glm::vec2 dir) { m_MoveDirection = dir; }

    /// decel / inertial_vel (0x30): knockback speed scalar; >1.0 selects COAST.
    float Decel() const { return m_Decel; }
    void SetDecel(float v) { m_Decel = v; }

    /// damping / friction (0x24): per-tick coast decay factor (decel *= damping).
    float Damping() const { return m_Damping; }
    void SetDamping(float v) { m_Damping = v; }

    /// force_direction (0x34): knockback travel direction (COAST branch).
    glm::vec2 ForceDirection() const { return m_ForceDirection; }
    void SetForceDirection(glm::vec2 dir) { m_ForceDirection = dir; }

    /// this_reply_time (0x64): the ReplyingHP heal accumulator.
    float ReplyTimer() const { return m_ReplyTimer; }
    void SetReplyTimer(float v) { m_ReplyTimer = v; }

    /**
     * @brief Compute the rigidbody velocity for one fixed step and apply the
     *        coast decel decay. FAITHFUL: RGPetController__FixedUpdate @
     *        game_full.c:427251-427312.
     *
     * Selects the branch by decel(0x30) <= 1.0 (line 427270):
     *   - FOLLOW: velocity = move_direction * speed * (speed_rate + 1.0)
     *     (two Vector2.op_Multiply, lines 427281-427288). decel is untouched.
     *   - COAST:  velocity = force_direction * decel (line 427302), then the only
     *     state write-back decel(0x30) *= damping(0x24) (line 427309).
     * The Rigidbody2D.set_velocity (lines 427293/427308) is the owner write; the
     * composed velocity is returned here. The get_transform tail (line 427312) is
     * owner facing and not modelled. The awake gate (line 427266) is the caller's
     * guard -- this method assumes awake.
     *
     * @param speed     role_attribute.speed (field 0x40 + 0x10).
     * @param speedRate role_attribute.speed_rate (field 0x40 + 0x14).
     * @param outBranch filled with the branch that ran (FOLLOW or COAST).
     * @return the velocity vector the owner would write via set_velocity.
     */
    glm::vec2 FixedUpdate(float speed, float speedRate, MoveBranch &outBranch);

    /**
     * @brief Advance the self-heal cadence by @p deltaTime. FAITHFUL:
     *        RGPetController__ReplyingHP @ game_full.c:427317-427364.
     *
     * Gate (line 427333): only while hp < max_hp. Accumulate the timer by
     * deltaTime (line 427336). When timer >= reply_time2 + reply_time1
     * (line 427338): heal hp += max_hp / 5 (line 427344, INTEGER division), reset
     * timer = reply_time1 (line 427345), then clamp hp = min(hp, max_hp)
     * (lines 427351-427354). The get_gameObject "fully healed fx" tail
     * (line 427356) is owner and not modelled.
     *
     * @param hp     in/out: role_attribute.hp (field 0x40 + 0x1C). Healed/clamped.
     * @param maxHp  role_attribute.max_hp (field 0x40 + 0x18).
     * @param deltaTime  the fixed/frame delta accumulated into the heal timer.
     * @return true if a heal step fired this tick (timer crossed the interval).
     */
    bool ReplyingHP(int &hp, int maxHp, float deltaTime);

    /**
     * @brief Reflect move_direction off a surface normal. FAITHFUL:
     *        RGPetController__TurnTo @ game_full.c:427457-427477.
     *
     * move_direction(0x50/0x54) = Vector2.Reflect(move_direction, normal)
     * (lines 427472-427476), where Reflect(v, n) = v - 2*dot(v,n)*n.
     *
     * @param normal the surface normal to bounce the steering off of.
     */
    void TurnTo(glm::vec2 normal);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    glm::vec2 m_MoveDirection{0.0F, 0.0F};  // 0x50/0x54 (ctor: Vector2.zero)
    glm::vec2 m_ForceDirection{0.0F, 0.0F}; // 0x34
    float m_Decel = 0.0F;                    // 0x30 (inertial_vel)
    float m_Damping = 0.0F;                  // 0x24 (friction)
    float m_ReplyTimer = 0.0F;               // 0x64 (this_reply_time)
};

} // namespace Game

#endif /* GAME_RG_PET_CONTROLLER_HPP */
