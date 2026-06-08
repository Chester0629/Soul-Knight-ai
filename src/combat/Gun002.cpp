#include "combat/Gun002.hpp"

namespace Game {

// FAITHFUL: Gun002__Attack @ game_full.c:315855-315862.
//   (count & 1) == 0 -> VectorSignedToFloat(-(count / 2))   [even]
//   else               iVar4 = -((count - 1) / 2)            [odd]
// VectorSignedToFloat is the ARM int->float vcvt intrinsic (a plain cast); the
// recoverable scalar is the signed start index itself. Integer `/2` truncates
// toward zero exactly as the decomp's `(int)count / 2`. NO RGRandom draw.
int Gun002::FanStartIndex(int count) {
    if ((count & 1) == 0) {
        return -(count / 2); // even fan: symmetric, no centre pellet.
    }
    return -((count - 1) / 2); // odd fan: centred straight pellet at index 0.
}

// FAITHFUL: Gun002__Attack -- pellet p sits at the start index plus its loop
// offset (the per-pellet spawn loop walks the fan upward). NO RGRandom draw.
int Gun002::PelletFanIndex(int count, int pelletIndex) {
    return FanStartIndex(count) + pelletIndex;
}

// FAITHFUL: Gun002__Attack -- fan slot * configured step angle (this+0x74).
// NO RGRandom draw.
float Gun002::PelletBaseAngle(int count, int pelletIndex, float stepAngle) {
    return static_cast<float>(PelletFanIndex(count, pelletIndex)) * stepAngle;
}

// FAITHFUL: Gun002__Attack @ 315885-315886:
//   fVar5 = (float)baseAngle(this+0x30);
//   fVar5 = fVar5 + fVar5 * *(float *)(obj + 0x20);   // obj = GetShootAngle ret
// == baseAngle * (1 + recoil). The owner's virtual GetShootAngle (*piVar2+0xf4)
// supplies the object whose +0x20 is the recoil multiplier; we take it as a
// parameter. NO RGRandom draw (this only sizes the half-span fed to Range()).
float Gun002::SpreadHalfSpan(float baseAngle, float recoil) {
    return baseAngle + baseAngle * recoil;
}

// FAITHFUL: Gun002__Attack @ 315890 -- RGRandom__Range(this+0x60, -fVar5, fVar5).
// ONE max-inclusive float draw per fired pellet, symmetric scatter. This is the
// only stream-advancing path; pump once per pellet (0..count-1), in order.
float Gun002::ScatterPellet(float halfSpan) {
    return m_Rng.Range(-halfSpan, halfSpan);
}

} // namespace Game
