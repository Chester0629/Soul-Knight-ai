#include "combat/Gun012.hpp"

namespace Game {

// FAITHFUL: Gun012__Attack @ game_full.c:964282 --
//   fanStart = -((count + ((count << 0x1f) >> 0x1f)) / 2)
// The `(count << 31) >> 31` is an arithmetic right shift of bit 0 sign-extended:
// it yields -1 for an odd count and 0 for an even count. We reproduce the exact
// signed integer arithmetic (C++ '/' truncates toward zero, matching the ARM
// SDIV the decomp lowers to). NO RGRandom draw.
int Gun012::FanStartFor(int count) {
    const int oddBias = (count << 31) >> 31; // -1 if odd, 0 if even
    return -((count + oddBias) / 2);
}

int Gun012::FanStart() const {
    return FanStartFor(m_Count);
}

// FAITHFUL: Gun012__Attack @ game_full.c:964295-964296 --
//   fVar4 = (float)VectorSignedToFloat(iVar3, ...)  // baseAngle int->float
//   fVar4 = fVar4 + fVar4 * *(float *)(iVar1 + 0x20)  // spread = base + base*recoil
// baseAngle is this+0x30 (decomp iVar3 via VectorSignedToFloat); recoil is the
// float at +0x20 of the virtual *(*(this+0x50)+0xf4) call's result (an
// owner-provided recoil/accuracy factor). The sibling Gun002__Attack @ 315830
// computes the identical spread. NO RGRandom draw.
float Gun012::ScatterHalfAngle(float recoil) const {
    return m_BaseAngle + m_BaseAngle * recoil;
}

// FAITHFUL: Gun012__Attack @ game_full.c:964301 --
//   RGRandom::Range(this+0x60, -spread, +spread)
// The single, symmetric scatter draw the recovered body makes. Range(float) is
// max-INCLUSIVE. Advances the stream exactly once.
float Gun012::RollScatter(float halfAngle) {
    return m_Rng.Range(-halfAngle, halfAngle);
}

} // namespace Game
