#include "combat/GunStaffWizard.hpp"

namespace Game {

// FAITHFUL: GunStaffWizard__Attack @ game_full.c:968628-968644 (the cyclic
// 4-state phase counter). The decomp switches on param_1[0x1e] and maps each
// covered case to the next phase, then writes it back to the same field:
//   case 0 -> 1; case 1 -> 2; case 2 -> 3; case 3 -> 0; param_1[0x1e] = next;
// The default case (968641) does `goto switchD..._default`, jumping PAST the
// write-back -- so an out-of-range value is left unchanged. We return the next
// phase (or the input unchanged when it is not 0..3) and let the owner store it.
// This runs FIRST every Attack, before the sign-gate. NO RGRandom draw.
int GunStaffWizard::NextPhase(int phase) {
    switch (phase) {
    case 0:
        return 1; // 968630
    case 1:
        return 2; // 968633
    case 2:
        return 3; // 968636
    case 3:
        return 0; // 968639
    default:
        return phase; // 968641: default skips the write-back (unchanged)
    }
}

// FAITHFUL: GunStaffWizard__Attack @ game_full.c:968667-968668 (firing branch,
// taken when the sign-gate param_1[0x1c] >= 0). spread = base + base*deviation =
// base*(1 + deviation), where `base` is the owner's base scatter field
// (owner+0x30, widened to float by VectorSignedToFloat) and `deviation` is a
// bullet-component float (component+0x20) supplied by the owner's vtable+0xf4
// fetch. This mirrors the canonical single-shot gun scatter shape
// (cf. Gun019::SpreadHalfAngle). NO RGRandom draw here.
float GunStaffWizard::SpreadHalfAngle(float base, float deviation) {
    return base + base * deviation;
}

// FAITHFUL: GunStaffWizard__Attack @ game_full.c:968670 --
// RGRandom__Range(param_1[0x18], -fVar4, fVar4, 0): ONE max-INCLUSIVE float
// draw, the symmetric per-shot scatter. Preserve exactly this single draw per
// firing shot. The muzzle Component.get_transform bullet spawn that follows
// (968672) is the OWNER's concern (truncated tail in the decomp). The decomp
// guards this draw with `if (param_1[0x18] != 0)` (the rng stream pointer is
// non-null); our RGRandom member is always present, so the guard is the owner's
// have-a-stream check, not part of the scatter logic.
float GunStaffWizard::ScatterAngle(float spread) {
    return m_Rng.Range(-spread, spread);
}

} // namespace Game
