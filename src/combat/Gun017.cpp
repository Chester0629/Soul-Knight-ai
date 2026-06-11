#include "combat/Gun017.hpp"

namespace Game {

// FAITHFUL: Gun017__Attack @ game_full.c:964753-964754. Identical to the
// canonical pistol scatter (Gun001__Attack @ 315779):
//   fVar4 = (float)VectorSignedToFloat(uVar3, ...);    // base spread angle 0x30
//   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20);  // + base * recoil
// `recoil` (iVar1+0x20) comes from the BLOCKED controller vtable walk
// (param_1+0x50 -> *0xf4); the owner reads it and hands it in here. The
// VectorSignedToFloat is the engine's int-bits-to-float reinterpret of the
// stored angle -- the owner already provides the angle as a float. No RGRandom
// draw: this is the pure scalar that FEEDS the Range draw in ScatterAngle.
float Gun017::BaseSpreadWithRecoil(float baseAngle, float recoil) {
    return baseAngle + baseAngle * recoil;
}

// FAITHFUL: Gun017__Attack @ game_full.c:964759:
//   RGRandom__Range(*(param_1 + 0x60), -fVar4, fVar4, 0);
// Exactly ONE float draw (max INCLUSIVE) per fired shot, symmetric about 0.
// The muzzle Transform.get_position spawn tail (964756) is OWNER.
float Gun017::ScatterAngle(float spread) {
    return m_Rng.Range(-spread, spread);
}

// FAITHFUL: Gun017__Attack @ game_full.c:964739:
//   if (*(char *)(*(param_1 + 0x6c) + 0xc) != '\0') return;
// While the drone is out (child+0xc byte set) the parent fires nothing and
// takes NO RGRandom draw. We model only the predicate so the draw count stays
// exact; the byte read is the owner's.
bool Gun017::CanFire(int droneActiveByte) {
    return droneActiveByte == 0;
}

// FAITHFUL: Gun017__StartUseWeapon @ game_full.c:964858. The body builds bVar1
// from the BLOCKED RGController-type test (param_1+0x50 -> 0xac/0x58 vtable
// walk); when true the drone is retracted (964884: StopRunning + ShowSelf),
// otherwise deployed (964889: StartRunning + HideSelf). RGWeapon.StartUseWeapon
// (964867) is the base-class side-effect (OWNER). The owner passes the bVar1
// result as controllerIsRGController.
bool Gun017::StartUseWeapon(bool controllerIsRGController) {
    // bVar1 -> retract (drone stowed, gun shown); else -> deploy (drone out,
    // gun hidden).
    m_Deployed = !controllerIsRGController;
    return m_Deployed;
}

// FAITHFUL: Gun017__StopWeapon @ game_full.c:964932. Three-way branch:
//   (A) isRGController && ownerWeaponIsSelf (964958): StartRunning + HideSelf
//       -> DEPLOY (964960). Decompiler "return" at 964962.
//   (B) isRGController && !ownerWeaponIsSelf (964967-964974): re-fetches piVar1
//       (same non-null ptr), second RGController type check passes -> plain
//       "return" at 964973; NO write to m_Deployed (state unchanged).
//   (C) !isRGController (LAB_00b064b0, 964976): StopRunning + ShowSelf
//       -> RETRACT (964978-964979).
// RGWeapon.StopWeapon (964942) is the base-class side-effect (OWNER). Both
// BLOCKED engine tests are supplied by the owner.
bool Gun017::StopWeapon(bool controllerIsRGController, bool ownerWeaponIsSelf) {
    if (controllerIsRGController) {
        if (ownerWeaponIsSelf) {
            // Case A: deploy.
            m_Deployed = true;
        }
        // Case B: !ownerWeaponIsSelf -> second type check passes -> return
        // without touching m_Deployed.  State is preserved exactly as in the
        // decomp (964973 plain return, no write).
        return m_Deployed;
    }
    // Case C: not RGController -> fall to LAB_00b064b0 -> retract.
    m_Deployed = false;
    return m_Deployed;
}

// FAITHFUL: Gun017__DropWeapon @ game_full.c:964807: RGWeapon.DropWeapon ->
// Gun017Child.StopRunning -> ShowSelf. A dropped weapon always retracts the
// drone and shows the gun. RGWeapon.DropWeapon (964809) is OWNER.
bool Gun017::DropWeapon() {
    m_Deployed = false;
    return m_Deployed;
}

} // namespace Game
