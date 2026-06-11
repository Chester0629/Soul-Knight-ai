#ifndef GAME_NPC_MERCENARY_CONTROLLER_HPP
#define GAME_NPC_MERCENARY_CONTROLLER_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class NpcMercenaryController
 * @brief Faithful decision/cadence brain for the hireable Mercenary NPC (an
 *        RGEController-derived ally, sibling to WolfController / NpcSummon01 /
 *        RGBatteryController).
 *
 * The Mercenary is a recruited follower that scouts toward its owner, wanders
 * when idle and fires its mounted weapon on a scheduled cadence. Unlike the pets
 * it has TWO weapon archetypes selected by a melee flag (byte 0xa8): a melee
 * branch (MeleeScout / MeleeShootReflection) and a ranged branch (RemoteScout /
 * RemoteShootReflection / RemoteRunReflection). ShootReflection is a pure
 * dispatcher on that flag.
 *
 * Modelled here (all pure; no Unity types). Only the recoverable
 * decision/cadence/scalar/state math is ported; every owner concern (rigidbody
 * velocity, Transform facing, PetHand, Invoke scheduling, RGWeapon item-level
 * reads, weapon-type RTTI, UI/coroutine) is left to the owner and cited at its
 * decomp site:
 *
 *   - ShootReflection(): pure dispatch on the melee flag (byte 0xa8): set ->
 *     MeleeShootReflection, clear -> RemoteShootReflection. No RNG, no write.
 *   - MeleeShootReflection(): the unconditional rg_random.Range(0, 10) draw (max
 *     EXCLUSIVE), the 8/10 gate ANDed with the can-shoot byte (0x71); on a pass:
 *     clear 0x71 and report the shoot cadence base(word 0x1b) + itemLevel * 0.25
 *     the owner passes to Invoke("Shoot", ...). The roll is drawn BEFORE the
 *     gate, so a gated-out shot still advances the stream.
 *   - RemoteShootReflection(): the unconditional rg_random.Range(0, 10) draw, the
 *     same 8/10 && can-shoot gate; on a pass: clear 0x71 and report the shoot
 *     cadence base(word 0x1b) * (itemLevel * 0.3 + 1.0). The interior weapon-type
 *     branch (GunShield / Gun011 / Gun007 RTTI) only chooses which PetHand call
 *     the owner makes -- it is owner RTTI and is NOT modelled; it takes no draw.
 *   - RemoteRunReflection(): the unconditional rg_random.Range(0, 10) wander draw
 *     after the detect virtual call; the roll<8 branch only selects which
 *     transform the owner reads for facing. The recoverable effect is the single
 *     draw plus move_direction (0x50/0x54) = Vector2.zero and the run cadence
 *     (word 0x1a) the owner passes to Invoke("RunReflection", ...).
 *   - MeleeScout()/RemoteScout(): the only state write is target_obj (word 7 ==
 *     byte 0x1c) = the owner/master ref (word 0x1d == byte 0x74). Detect virtual
 *     (vtable 0xec) and the run-reflection re-dispatch (vtable 0x12c) are owner.
 *     No RNG.
 *   - EndCycle(): move_direction (0x50/0x54) = Vector2.zero. CancelInvoke and
 *     anim.SetBool("walk", false) are owner. No RNG.
 *   - GetHurt(): the alive gate (byte 0x0d == 0): a live mercenary routes the hit
 *     to UICanvas (the floating damage number); a dead one ignores it. Touches NO
 *     vitals (HP is applied by the base damage chain). No RNG.
 *   - Dead(): the already-dead gate (byte 0x0d != 0 -> early return). The rest
 *     (anim.SetTrigger("dead") gated on byte 0x85, get_transform) is owner. No
 *     RNG.
 *
 * Owner-only bodies (NOT modelled; one-line note here, cited at the decomp site):
 *   - FixedUpdate (1670409): the Rigidbody2D velocity composition -- two
 *     Vector2.op_Multiply (move_direction * speed_rate fields @0x10+0x10/+0x14)
 *     and the knockback branch that decays the impulse by
 *     impulse(word 0xc) *= friction(word 9) each FixedUpdate (line 1670491).
 *     All set_velocity / get_transform writes are Unity rigidbody concerns.
 *   - FixedRotation (1670843): Object.op_Implicit + Transform.get_position facing.
 *   - TalkGetMercenary (1671048): String.IsNullOrEmpty(0x8c) -> UICanvas (UI).
 *   - SetUpWeapon (1671127): get_transform stub.
 *   - ResetIsCharge (1671142): pure weapon-type RTTI (Gun003/006/007/011/GunDrill/
 *     GunOnePunch/GunChannel/GunChain TypeInfo isinst) writing the charge flags
 *     (byte 0xc0 / uint 0xb0). The decision depends entirely on weapon types we
 *     do not model -> owner RTTI, no RNG.
 *   - CanPickWeapon (1671212): Singleton<NetControllerManager>.get_Inst.
 *   - StartPickWeapon (1671230): CanPickWeapon gate -> UICanvas.
 *   - PeakingWeapon (1671256): coroutine (<PeakingWeapon>c__Iterator0).
 *   - StopPickWeapon (1671269): Object.op_Equality(0xc4) -> clear 0xc4 / 0xc1
 *     (owner Object identity; not pure).
 *
 * OFFSET MAP (RGEController-derived; word index n == byte n*4; decoded from this
 * class's own accesses, cross-checked against the RGEController table). The
 * decomp types param_1 as int* (word index) in the Shoot/Run/Scout/FixedUpdate
 * bodies and int (byte index) in EndCycle/GetHurt/Dead -- both resolve to the
 * same byte offsets:
 *   0x0d dead/alive latch (byte; GetHurt + Dead gate),
 *   0x1c target_obj (word 7; Scout write),
 *   0x50/0x54 move_direction (words 0x14/0x15; EndCycle + RunReflection write),
 *   0x68 run cadence period (word 0x1a; RunReflection Invoke delay),
 *   0x6c base shoot cadence (word 0x1b; ShootReflection cadence base),
 *   0x71 can-shoot byte (byte; ShootReflection gate, cleared on fire),
 *   0x74 owner/master ref (word 0x1d; Scout target source),
 *   0x85 dead-anim flag (byte; Dead branch, owner),
 *   0xa8 melee flag (byte; ShootReflection dispatch).
 *
 * @see IL2CPP skeleton NpcMercenaryController (RGEController subclass);
 *      FAITHFUL: NpcMercenaryController @ game_full.c:1670300-1671292.
 */
class NpcMercenaryController {
public:
    /// Fire/wander roll ceiling (rg_random.Range(0, 10), max EXCLUSIVE).
    static constexpr int kRollCeiling = 10;
    /// The roll must be strictly below this to pass the gate (iVar1 < 8).
    static constexpr int kRollThreshold = 8;
    /// Melee shoot-cadence scalar: base + itemLevel * 0.25 (line 1670695).
    static constexpr float kMeleeItemLevelStep = 0.25F;
    /// Remote shoot-cadence scalar: base * (itemLevel * 0.3 + 1.0) (line 1670804).
    static constexpr float kRemoteItemLevelStep = 0.3F;
    /// Remote shoot-cadence multiplier base added to itemLevel*step (line 1670804).
    static constexpr float kRemoteCadenceBias = 1.0F;
    /// ctor-state of the can-shoot byte (0x71): a fresh mercenary is armed.
    static constexpr bool kCanShootInitial = true;

    /**
     * @brief Outcome of one Shoot*Reflection tick, mirroring the decomp's gate.
     *
     *   - Fired: roll < 8 AND can-shoot byte (0x71) != 0 -> clear 0x71 and the
     *            owner schedules Invoke("Shoot", cadence). The cadence formula
     *            differs per branch (melee additive, remote multiplicative).
     *   - Held:  the draw was taken but the gate failed (roll >= 8, or can-shoot
     *            was already 0) -> no flag write, no Invoke. The stream still
     *            advanced (the draw is unconditional, before the gate).
     */
    enum class ShootResult {
        Fired, ///< gate passed: 0x71 cleared, owner Invokes the shot.
        Held   ///< draw taken but gate failed: no write.
    };

    /**
     * @brief Which Shoot*Reflection branch ShootReflection dispatched to.
     *        FAITHFUL: NpcMercenaryController__ShootReflection @
     *        game_full.c:1670499 -- selected by the melee flag (byte 0xa8).
     */
    enum class ShootBranch {
        Melee, ///< melee flag (0xa8) != 0 -> MeleeShootReflection.
        Remote ///< melee flag (0xa8) == 0 -> RemoteShootReflection.
    };

    NpcMercenaryController() = default;

    /// Seed this mercenary's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----

    /// melee flag (byte 0xa8): selects the Scout/ShootReflection branch.
    bool Melee() const { return m_Melee; }
    void SetMelee(bool v) { m_Melee = v; }

    /// can-shoot byte (0x71): gates the fire branch of both Shoot*Reflections.
    bool CanShoot() const { return m_CanShoot; }
    void SetCanShoot(bool v) { m_CanShoot = v; }

    /// dead/alive latch (byte 0x0d): gates GetHurt and Dead. Named IsDead() to
    /// avoid colliding with the faithful Dead() method (NpcMercenaryController__Dead).
    bool IsDead() const { return m_Dead; }
    void SetDead(bool v) { m_Dead = v; }

    /// Whether Scout has pointed target_obj (0x1c) at the owner ref (0x74).
    bool TargetIsOwner() const { return m_TargetIsOwner; }

    /// Last move_direction written by EndCycle/RemoteRunReflection (0x50/0x54).
    glm::vec2 MoveDirection() const { return m_MoveDirection; }

    /// Last roll drawn by a Shoot*Reflection / RunReflection; -1 before any draw.
    int LastRoll() const { return m_LastRoll; }

    /**
     * @brief Dispatch a shot to the melee or remote branch. FAITHFUL:
     *        NpcMercenaryController__ShootReflection @ game_full.c:1670499.
     *
     * Pure dispatcher: reads the melee flag (byte 0xa8, line 1670502) and calls
     * MeleeShootReflection when set, else RemoteShootReflection. It performs NO
     * draw and NO write of its own; the draw and gate live in the branch it
     * forwards to (the out-params/result flow through).
     * @param outCadence filled with the branch's shoot cadence when Fired.
     * @param baseCadence this mercenary's base shoot cadence field (word 0x1b).
     * @param itemLevel   the mounted weapon's item level (owner reads it via
     *                    RGWeapon.GetItemLevel; supplied here as the cadence
     *                    scalar). Drawn from the weapon, not the RNG stream.
     * @param outBranch   which branch ran (Melee / Remote).
     * @return the branch's ShootResult (Fired / Held). One Range(0,10) draw.
     */
    ShootResult ShootReflection(float &outCadence, float baseCadence, int itemLevel,
                                ShootBranch &outBranch);

    /**
     * @brief Roll-and-gate one melee shot. FAITHFUL:
     *        NpcMercenaryController__MeleeShootReflection @ game_full.c:1670647.
     *
     * Draws rg_random.Range(0, 10) UNCONDITIONALLY (line 1670659, max EXCLUSIVE)
     * BEFORE the gate, so a gated-out shot still advances the stream. The gate
     * (line 1670660) is `roll < 8 && can_shoot byte(0x71) != 0`. On a pass it
     * clears 0x71 (line 1670661) and the owner schedules Invoke("Shoot", cadence)
     * with cadence = base(word 0x1b) + itemLevel * 0.25 (line 1670695). The
     * RGWeapon.GetItemLevel read (line 1670692), PetHand.SetAttackTrigger and the
     * vtable-0x134 jumptable re-dispatch ("Could not recover jumptable at
     * 0x0119c070") are owner and are NOT modelled.
     * @param outCadence filled with base + itemLevel*0.25 only when Fired.
     * @param baseCadence base shoot cadence field (word 0x1b).
     * @param itemLevel   the weapon item level (owner-supplied scalar; no draw).
     * @return Fired if the gate passed (0x71 cleared), else Held.
     */
    ShootResult MeleeShootReflection(float &outCadence, float baseCadence, int itemLevel);

    /**
     * @brief Roll-and-gate one ranged shot. FAITHFUL:
     *        NpcMercenaryController__RemoteShootReflection @ game_full.c:1670707.
     *
     * Draws rg_random.Range(0, 10) UNCONDITIONALLY (line 1670721, max EXCLUSIVE)
     * BEFORE the gate. The gate (line 1670722, written as `(7 < roll) ||
     * (0x71 == 0)` -> skip) proceeds only when `roll < 8 && can_shoot(0x71) != 0`.
     * On a pass it clears 0x71 (line 1670723) and the owner schedules
     * Invoke("Shoot", cadence) with cadence = base(word 0x1b) *
     * (itemLevel * 0.3 + 1.0) (line 1670804). The interior weapon-type branch
     * (GunShield / Gun011 / Gun007 RTTI, lines 1670728-1670800) only selects which
     * PetHand.SetAttack / GetComponent the owner calls -- it is owner RTTI, takes
     * no draw, and is NOT modelled. The vtable-0x134 jumptable re-dispatch
     * (line 1670808) is owner.
     * @param outCadence filled with base * (itemLevel*0.3 + 1.0) only when Fired.
     * @param baseCadence base shoot cadence field (word 0x1b).
     * @param itemLevel   the weapon item level (owner-supplied scalar; no draw).
     * @return Fired if the gate passed (0x71 cleared), else Held.
     */
    ShootResult RemoteShootReflection(float &outCadence, float baseCadence, int itemLevel);

    /**
     * @brief Re-pick wander state (ranged branch). FAITHFUL:
     *        NpcMercenaryController__RemoteRunReflection @ game_full.c:1670550.
     *
     * After a detect virtual call (vtable 0xe4, line 1670569) the body draws
     * rg_random.Range(0, 10) UNCONDITIONALLY (line 1670578, max EXCLUSIVE). The
     * detect-result + roll<8 branches only select which transform the owner reads
     * for facing (get_position on target word 7, or get_transform on self) -- owner
     * reads, not modelled. The recoverable pure effect is the single draw plus
     * move_direction (words 0x14/0x15 == bytes 0x50/0x54, lines 1670586-1670587)
     * = Vector2.zero and the run cadence (word 0x1a) the owner passes to
     * Invoke("RunReflection", ...) (line 1670588).
     * @param outRunCadence filled with the run cadence field (word 0x1a) the owner
     *                      schedules the next RunReflection on.
     * @param runCadence    this mercenary's run cadence field (word 0x1a) value.
     * @return the rg_random.Range(0, 10) wander roll (the advancing draw).
     */
    int RemoteRunReflection(float &outRunCadence, float runCadence);

    /**
     * @brief Target-acquisition tick (melee branch). FAITHFUL:
     *        NpcMercenaryController__MeleeScout @ game_full.c:1670512.
     *
     * The only state write is target_obj (word 7 == byte 0x1c) = the owner ref
     * (word 0x1d == byte 0x74, line 1670520). The detect virtual (vtable 0xec,
     * line 1670519), the conditional get_transform (line 1670523) and the
     * run-reflection re-dispatch (vtable 0x12c, line 1670525) are owner. Takes NO
     * rg_random draw of its own.
     */
    void MeleeScout();

    /**
     * @brief Target-acquisition tick (ranged branch). FAITHFUL:
     *        NpcMercenaryController__RemoteScout @ game_full.c:1670531.
     *
     * Structurally identical to MeleeScout: the only state write is target_obj
     * (0x1c) = the owner ref (0x74, line 1670539); detect virtual (vtable 0xec),
     * conditional get_transform and the run-reflection re-dispatch (vtable 0x12c)
     * are owner. Takes NO rg_random draw of its own.
     */
    void RemoteScout();

    /**
     * @brief Stop the AI cycle. FAITHFUL:
     *        NpcMercenaryController__EndCycle @ game_full.c:1670814.
     *
     * The only recoverable state write is move_direction (bytes 0x50/0x54) =
     * Vector2.zero (lines 1670830-1670832). Owner: CancelInvoke("RunReflection")
     * (line 1670824) and anim.SetBool("walk", false) (line 1670838). No dead-gate,
     * no RNG.
     */
    void EndCycle();

    /**
     * @brief Take a hit. FAITHFUL:
     *        NpcMercenaryController__GetHurt @ game_full.c:1670954.
     *
     * Gate (line 1670961): only a live (byte 0x0d == 0) mercenary routes the hit
     * to UICanvas.GetInstance() (the floating damage number). A dead one ignores
     * it. This override touches NO vitals (HP is applied by the base damage chain);
     * modelling any HP change here would be fabrication. No RNG.
     * @return true if the hit was routed (mercenary alive); false if ignored.
     */
    bool GetHurt() const;

    /**
     * @brief Latch death. FAITHFUL:
     *        NpcMercenaryController__Dead @ game_full.c:1670972.
     *
     * The recoverable gate (line 1670979): if already dead (byte 0x0d != 0) the
     * whole body is skipped (no write). NOTE: this override does NOT set the dead
     * latch itself -- the decomp reads 0x0d but never writes it here (the base
     * damage chain owns the latch), so modelling a write would be fabrication.
     * The rest (anim.SetTrigger("dead") gated on byte 0x85 @ line 1670982, the
     * get_transform spawn @ line 1670992) is owner. No RNG.
     * @return true if Dead would run its (owner) body (was alive); false if the
     *         already-dead gate short-circuited it.
     */
    bool Dead() const;

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Melee = false;             // 0xa8 melee flag
    bool m_CanShoot = kCanShootInitial; // 0x71 can-shoot byte
    bool m_Dead = false;              // 0x0d dead/alive latch
    bool m_TargetIsOwner = false;     // target_obj (0x1c) == owner ref (0x74)
    glm::vec2 m_MoveDirection{0.0F, 0.0F}; // 0x50/0x54 move_direction
    int m_LastRoll = -1;              // last Range(0,10) roll; -1 before any draw
};

} // namespace Game

#endif /* GAME_NPC_MERCENARY_CONTROLLER_HPP */
