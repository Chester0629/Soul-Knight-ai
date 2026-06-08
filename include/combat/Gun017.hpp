#ifndef GAME_GUN017_HPP
#define GAME_GUN017_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun017
 * @brief Faithful scatter + drone deploy/retract state machine for the "Gun017"
 *        weapon (Soul Knight 1.7.10).
 *
 * Per-content port. Gun017 is a drone-deploy PARENT weapon: it owns a homing
 * Gun017Child drone (field this+0x6c) which it can deploy (StartRunning + hide
 * the gun) or retract (StopRunning + show the gun). The PARENT itself fires a
 * single-shot scatter just like the canonical pistol (Gun001): one symmetric
 * RGRandom::Range(-spread, +spread) draw around the aim, with the spread widened
 * by the wielding controller's recoil.
 *
 * Only the recoverable pure scalar / state math is modelled here; everything
 * touching the engine or the blocked child is the OWNER's concern:
 *   - the wielding-controller vtable walk (param_1+0x50 -> *0xf4 sub-controller,
 *     then its recoil at +0x20) is a BLOCKED controller-type read -- the owner
 *     supplies the resulting recoil scalar to BaseSpreadWithRecoil;
 *   - the RGController-type test (the 0xac / 0x58 type-id vtable walk shared by
 *     ShowSelf / HideSelf / StartUseWeapon / StopWeapon) is BLOCKED -- the owner
 *     supplies the resulting bool;
 *   - the op_Equality(weapon owner-slot, this) "is this the local player's
 *     weapon" test in StopWeapon (piVar1[0x10].field+0x20 == this) is BLOCKED --
 *     owner supplies the bool;
 *   - the muzzle Transform.get_position spawn tail of Attack, the bullet
 *     Instantiate / PrefabPool spawn, and Component.get_transform show/hide
 *     side-effects are all OWNER;
 *   - the Gun017Child homing drone itself (movement, targeting, its own attacks)
 *     is BLOCKED and intentionally NOT ported.
 *
 * What IS modelled (the honest, small brain):
 *   - Attack (964721): the scatter angle -- spread = baseAngle + baseAngle*recoil
 *     (field 0x30 base angle, controller +0x20 recoil), then one symmetric
 *     RGRandom::Range(-spread, +spread) draw; plus the "drone is out -> don't
 *     fire" gate (child+0xc byte != 0 -> return early, no draw).
 *   - the deploy / retract boolean state machine driven by StartUseWeapon
 *     (964858), StopWeapon (964932) and DropWeapon (964807): each resolves to
 *     "deploy the drone + hide the gun" or "retract the drone + show the gun".
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun017 @ game_full.c:964703-964987 (parent only).
 *      Canonical scatter cross-check: Gun001__Attack @ 315755 (identical shape).
 */
class Gun017 {
public:
    Gun017() = default;

    /// Seed the weapon's deterministic stream (call once at spawn). The scatter
    /// path draws exactly one float per fired shot; seeding keeps it in lockstep
    /// with the original RGRandom stream (this+0x60).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- Attack scatter (964721) ------------------------------------------

    /**
     * @brief Effective spread half-angle (degrees): baseAngle + baseAngle*recoil.
     *
     * FAITHFUL: Gun017__Attack @ game_full.c:964753-964754:
     *   fVar4 = VectorSignedToFloat(uVar3, ...);           // base spread angle
     *   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20); // + base*recoil
     * `baseAngle` is the weapon field this+0x30; `recoil` is the recoil
     * multiplier at +0x20 on the sub-controller returned by the BLOCKED
     * controller vtable walk (param_1+0x50 -> *0xf4) -- the owner reads it and
     * passes it here. NO RGRandom draw in this step (it is a pure scalar that
     * FEEDS the Range draw below). Matches Gun001__Attack @ 315779.
     */
    static float BaseSpreadWithRecoil(float baseAngle, float recoil);

    /**
     * @brief Draw the symmetric scatter angle: RGRandom::Range(-spread, +spread).
     *
     * FAITHFUL: Gun017__Attack @ game_full.c:964759:
     *   RGRandom__Range(*(param_1 + 0x60), -fVar4, fVar4, 0);
     * Exactly ONE float draw (max INCLUSIVE) per call, in [-spread, +spread].
     * The muzzle Transform.get_position / bullet spawn tail (964756) is OWNER.
     * @param spread the half-angle from BaseSpreadWithRecoil (decomp fVar4).
     * @return the scattered offset angle in degrees.
     */
    float ScatterAngle(float spread);

    /**
     * @brief Whether Attack may fire this pull (the drone-out gate).
     *
     * FAITHFUL: Gun017__Attack @ game_full.c:964739:
     *   if (*(char *)(*(param_1 + 0x6c) + 0xc) != '\0') return;
     * The child byte at +0xc is the drone's "deployed / busy" flag: while the
     * drone is out the parent does NOT fire (and therefore takes NO RGRandom
     * draw). The owner reads the child byte; we model only the predicate so the
     * draw count stays exact.
     * @param droneActiveByte the Gun017Child flag at child+0xc.
     * @return true iff the parent should run its scatter+spawn this pull.
     */
    static bool CanFire(int droneActiveByte);

    // ---- deploy / retract state machine (964858 / 964932 / 964807) --------

    /// True once the drone has been deployed (StartRunning + gun hidden); false
    /// while retracted (StopRunning + gun shown). Mirrors the parent's visible
    /// state -- the original recomputes the branch each call rather than storing
    /// a bool, so this is the recovered consequence of those branches.
    bool Deployed() const { return m_Deployed; }

    /**
     * @brief Resolve StartUseWeapon's deploy/retract toggle (964858).
     *
     * FAITHFUL: Gun017__StartUseWeapon @ game_full.c:964858. After
     * RGWeapon.StartUseWeapon, the body computes `bVar1` from the BLOCKED
     * RGController-type vtable walk (param_1+0x50 -> 0xac/0x58 type test):
     *   - bVar1 (964884) -> Gun017Child.StopRunning + ShowSelf  (RETRACT);
     *   - else  (964889) -> Gun017Child.StartRunning + HideSelf (DEPLOY).
     * The owner supplies `controllerIsRGController` (the bVar1 result). Updates
     * and returns Deployed().
     * @return true if the drone is now deployed (gun hidden), false if retracted.
     */
    bool StartUseWeapon(bool controllerIsRGController);

    /**
     * @brief Resolve StopWeapon's deploy/retract toggle (964932).
     *
     * FAITHFUL: Gun017__StopWeapon @ game_full.c:964932. Three-way outcome:
     *   (A) controllerIsRGController=true AND ownerWeaponIsSelf=true  (964958):
     *       DEPLOY (StartRunning + HideSelf, 964960); returns true.
     *   (B) controllerIsRGController=true AND ownerWeaponIsSelf=false (964967-
     *       964974): re-fetches piVar1, second RGController type check passes
     *       -> plain return, NO state change; returns Deployed() unchanged.
     *   (C) controllerIsRGController=false (964976 LAB_00b064b0): RETRACT
     *       (StopRunning + ShowSelf, 964978); returns false.
     * Both BLOCKED engine tests are supplied by the owner.
     * @param controllerIsRGController the 0xac/0x58 type test result (964945).
     * @param ownerWeaponIsSelf op_Equality(piVar1[0x10].+0x20, this) (964957).
     * @return Deployed() after the call (unchanged for case B).
     */
    bool StopWeapon(bool controllerIsRGController, bool ownerWeaponIsSelf);

    /**
     * @brief Resolve DropWeapon's unconditional retract (964807).
     *
     * FAITHFUL: Gun017__DropWeapon @ game_full.c:964807: RGWeapon.DropWeapon ->
     * Gun017Child.StopRunning(child) -> ShowSelf. Dropping the weapon always
     * retracts the drone and shows the gun. Sets Deployed() to false.
     * @return false (always retracted after a drop).
     */
    bool DropWeapon();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Deployed = false; ///< drone out (gun hidden) vs retracted (gun shown).
};

} // namespace Game

#endif /* GAME_GUN017_HPP */
