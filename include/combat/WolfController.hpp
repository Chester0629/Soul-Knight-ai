#ifndef GAME_WOLF_CONTROLLER_HPP
#define GAME_WOLF_CONTROLLER_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class WolfController
 * @brief Faithful decision/cadence brain for the Wolf pet (an RGPetController
 *        subclass: RGBaseController -> RGPetController -> WolfController).
 *
 * NOTE ON THE OFFSET MAP: unlike the EnemyAINN/BossAINN ports, the Wolf is NOT
 * an RGEController; it is an RGPetController, so the RGEController BASE offsets
 * (0x18 awake, 0x7C target_obj, ...) do NOT apply. The offsets below were
 * decoded directly from the decomp's own self-consistent accesses (cross-checked
 * against RGPetController__FixedUpdate / __EndCycle / __TurnTo / __Scout and
 * RGBaseController layout):
 *   awake @ 0x0C, anim @ 0x14, rigibody @ 0x18, target_obj @ 0x1C,
 *   friction @ 0x24, inertial_vel @ 0x30, force_direction @ 0x34 (Vector2),
 *   role_attribute @ 0x40, move_direction @ 0x50 (Vector2),
 *   master_tf @ 0x74, and the WolfController-local fields
 *   strengthen @ 0x80 (bool), bullet1 @ 0x84 (ptr), bullet2 @ 0x88 (ptr),
 *   rg_random @ 0x90 (ptr).
 *
 * The Wolf is a master-following pet: Scout points its target at its master and
 * dispatches FixedRotation (facing toward target) + RunReflection (the single
 * deterministic wander draw). When it attacks it instantiates one of two weapon
 * prefabs chosen by the `strengthen` flag.
 *
 * Modelled here (all pure; no Unity types):
 *   - Scout(): the only state write the decomp performs is target_obj =
 *     master_tf (param_1[7] = param_1[0x1d], line 626862); then it dispatches
 *     FixedRotation + RunReflection (lines 626867-626868). Takes NO draw itself;
 *     the draw lives in RunReflection. The virtual detect call (vtable 0xec,
 *     line 626861) and the get_transform tail (line 626865) are indirect/owner
 *     and are NOT modelled.
 *   - RunReflection(): the single rg_random.Range(0, 10) wander/idle seed draw
 *     (line 626912, max EXCLUSIVE). The preceding virtual call (vtable 0xe4,
 *     line 626902) -> conditional get_transform (line 626905) is the
 *     facing/detect decision and is indirect/owner; not modelled.
 *   - EndCycle(): move_direction = Vector2.zero (0x50/0x54, lines 626933-626935).
 *     Owner: CancelInvoke("Scout") (line 626927) and anim.SetBool("walk", false)
 *     (line 626941). NO dead-gate in the Wolf override. No RNG.
 *   - OnAtk(): the weapon-prefab selection -- bullet1 (0x84) when NOT strengthen
 *     (0x80), else bullet2 (0x88) (lines 626958-626973). The Instantiate<RGWeapon>
 *     + GetComponent<RGSword> (lines 626974-626980) are owner. No RNG.
 *
 * NOT modelled (by design):
 *   - Awake (626793) / Start (626806): type-init guards + get_gameObject /
 *     get_transform / PrefabManager.GetPrefab tail calls. No pure logic, no RNG.
 *   - FixedUpdate (FUN_00767b38 @ 626820): the Rigidbody2D velocity composition
 *     (two Vector2 op_Multiply + set_velocity). The decomp body is register-soup
 *     truncated ("Subroutine does not return"); no recoverable gates, no friction
 *     decay branch, no RNG. It is a Unity rigidbody write -> owner concern.
 *   - FixedRotation (626874): target_obj null-guard + get_position tail-call
 *     (line 626888). The position read only feeds the owner's facing; no RNG.
 *
 * @see IL2CPP skeleton WolfController.cs (strengthen, bullet1, bullet2,
 *      audio_clip, rg_random, index, atk_point);
 *      FAITHFUL: WolfController @ game_full.c:626793-626983.
 */
class WolfController {
public:
    /// Wander/idle re-roll ceiling drawn by RunReflection
    /// (rg_random.Range(0, 10), max EXCLUSIVE).
    static constexpr int kWanderRerollCeiling = 10;

    /**
     * @brief Which weapon prefab OnAtk selects.
     *
     * FAITHFUL: WolfController__OnAtk @ game_full.c:626958 -- the prefab is
     * bullet1 when strengthen (0x80) is false, else bullet2.
     */
    enum class AtkPrefab {
        Bullet1, ///< strengthen == false -> bullet1 (field 0x84).
        Bullet2  ///< strengthen == true  -> bullet2 (field 0x88).
    };

    WolfController() = default;

    /// Seed this pet's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- mutable state (owning entity mirrors these into the real fields) ----

    /// strengthen flag (0x80): selects the attack prefab in OnAtk.
    bool Strengthen() const { return m_Strengthen; }
    void SetStrengthen(bool v) { m_Strengthen = v; }

    /// Whether Scout has pointed the target at the master (target_obj == master_tf).
    bool TargetIsMaster() const { return m_TargetIsMaster; }

    /// Whether EndCycle has zeroed move_direction (0x50/0x54 = Vector2.zero).
    bool MoveDirZeroed() const { return m_MoveDirZeroed; }

    /// Last wander roll drawn by RunReflection (Range(0, 10)); -1 before any draw.
    int LastWanderRoll() const { return m_LastWanderRoll; }

    /**
     * @brief Target-acquisition tick. FAITHFUL: WolfController__Scout @
     *        game_full.c:626854.
     *
     * The only state write the decomp performs is target_obj = master_tf
     * (param_1[7] = param_1[0x1d], line 626862): the pet targets its master. It
     * then dispatches FixedRotation (owner facing) and RunReflection (the wander
     * draw). Scout itself takes NO rg_random draw; the draw is taken inside
     * RunReflection, which this method calls -> the returned roll is propagated.
     * The virtual detect call (vtable 0xec) and the get_transform tail are
     * indirect/owner and are not modelled.
     * @return the wander roll RunReflection drew (Range(0, 10)).
     */
    int Scout();

    /**
     * @brief Re-roll the deterministic wander/idle seed. FAITHFUL:
     *        WolfController__RunReflection @ game_full.c:626893.
     *
     * Draws the single rg_random.Range(0, 10) (line 626912, max EXCLUSIVE) and
     * stores it as the last wander roll. The preceding virtual call (vtable 0xe4,
     * line 626902) -> conditional get_transform (line 626905) is the facing/detect
     * decision (indirect/owner) and is not modelled; the get_position tail (the
     * truncated FUN at line 626911 site) is owner too.
     * @return the rg_random.Range(0, 10) roll.
     */
    int RunReflection();

    /**
     * @brief Stop the AI cycle. FAITHFUL: WolfController__EndCycle @
     *        game_full.c:626917.
     *
     * Zeroes move_direction (0x50/0x54 = Vector2.zero, lines 626934-626935) and
     * NOTHING else: the decomp does NOT touch target_obj (0x1C), so the
     * targets-master latch is left UNCHANGED. Owner: CancelInvoke("Scout")
     * (line 626927) and anim.SetBool("walk", false) (line 626941). NOTE: the
     * Wolf override has NO dead-gate. No RNG.
     */
    void EndCycle();

    /**
     * @brief Select the weapon prefab to instantiate. FAITHFUL:
     *        WolfController__OnAtk @ game_full.c:626948.
     *
     * Returns bullet1 (0x84) when strengthen (0x80) is false, else bullet2
     * (0x88) (lines 626958-626973). Owner: Instantiate<RGWeapon>(prefab) +
     * GetComponent<RGSword> (lines 626974-626980). No RNG.
     * @return AtkPrefab::Bullet2 when strengthened, else AtkPrefab::Bullet1.
     */
    AtkPrefab OnAtk() const;

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    bool m_Strengthen = false;     // 0x80
    bool m_TargetIsMaster = false; // target_obj (0x1C) == master_tf (0x74)
    bool m_MoveDirZeroed = false;  // move_direction (0x50/0x54) == Vector2.zero
    int m_LastWanderRoll = -1;     // last RunReflection Range(0,10) roll
};

} // namespace Game

#endif /* GAME_WOLF_CONTROLLER_HPP */
