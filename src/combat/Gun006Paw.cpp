#include "combat/Gun006Paw.hpp"

namespace Game {

// FAITHFUL: Gun006Paw__StartUseWeapon @ game_full.c:317035 --
//   piVar3 = (int *)(piVar3[0x11] + 0x40); *piVar3 = *piVar3 + *(int *)(param_1 + 0xc);
// The resolved controller's role_attribute stat (+0x40) is increased by this
// weapon's atk (this+0xc). Pure int add, no clamp. The pointer chain that
// REACHES the stat (controller resolution + the role_attribute walk) is the
// OWNER's / type-walk's job; this models the recoverable arithmetic only.
int Gun006Paw::StatAfterBuff(int controllerStat, int atk) {
    return controllerStat + atk;
}

// FAITHFUL: Gun006Paw__StartUseWeapon @ game_full.c:317062 --
//   iVar2 = *(int *)(*(int *)(param_1 + 0x10) + 0x44);
//   *(undefined4 *)(iVar1 + 0x20) = *(undefined4 *)(iVar2 + 0x40);
// sword.dmg (iVar1 == get_sword, +0x20) is assigned the int read from the
// stat-source object reached via controller(this+0x10)+0x44 then +0x40. The
// decomp copies the value verbatim (undefined4 == 4-byte), so this is a pure
// pass-through: the new sword damage IS the supplied source-stat value.
int Gun006Paw::SwordDmgFromTarget(int targetSourceStat) {
    return targetSourceStat;
}

// FAITHFUL: Gun006Paw__StartUseWeapon @ game_full.c:317074 (LAB_003efbd4) --
//   *(int *)(iVar2 + 0x20) = *(int *)(param_1 + 0xc) + 3;
// Fallback when the controller-type walk does not resolve to an RGController:
// sword.dmg (sword + 0x20) = atk(this+0xc) + 3. Pure scalar.
int Gun006Paw::FallbackSwordDmg(int atk) {
    return atk + kFallbackDmgBonus;
}

} // namespace Game
