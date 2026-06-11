#include "combat/GunMultiBullet.hpp"

namespace Game {

// FAITHFUL: GunMultiBullet__GetAttack @ game_full.c:967541-967570.
//   if (*(attackArray 0x70 + 0xc) == *(bulletCount 0x6c + 0xc)) {
//       // bounds check 967561: *(attackArray + 0xc) <= index -> throw
//       return *(int *)(attackArray + index*4 + 0x10);   // 967565
//   } else {
//       return *(int *)(param_1 + 0x20);                 // 967568 scalar
//   }
// The override element is a raw int (attack value), index*4 stride. When the
// counts are equal the array is exactly bulletCount long, so a valid bullet
// index is always in range; UsesOverride folds the equal-count and in-range
// tests. NO RGRandom draw.
int GunMultiBullet::GetAttack(const std::vector<int> &attackOverride,
                              std::size_t bulletCount, std::size_t index,
                              int attackScalar) {
    if (UsesOverride(attackOverride.size(), bulletCount, index)) {
        return attackOverride[index];
    }
    return attackScalar;
}

// FAITHFUL: GunMultiBullet__GetSpeed @ game_full.c:967575-967606.
//   if (*(speedArray 0x74 + 0xc) == *(bulletCount 0x6c + 0xc)) {
//       // bounds check 967596: *(speedArray + 0xc) <= index -> throw
//       return VectorSignedToFloat(*(speedArray + index*4 + 0x10)); // 967600
//   } else {
//       return *(undefined4 *)(param_1 + 0x28);                     // 967604
//   }
// Asymmetry preserved in the doc: the array element is widened int->float via
// VectorSignedToFloat while the scalar fallback is returned raw. We take both as
// float (the owner's already-widened representation); the conversion of the
// array element is an owner storage detail. NO RGRandom draw.
float GunMultiBullet::GetSpeed(const std::vector<float> &speedOverride,
                               std::size_t bulletCount, std::size_t index,
                               float speedScalar) {
    if (UsesOverride(speedOverride.size(), bulletCount, index)) {
        return speedOverride[index];
    }
    return speedScalar;
}

// FAITHFUL: GunMultiBullet__GetCanThrough @ game_full.c:967611-967640.
//   if (*(throughArray 0x80 + 0xc) == *(bulletCount 0x6c + 0xc)) {
//       // bounds check 967631: *(throughArray + 0xc) <= index -> throw
//       return *(byte *)(throughArray + index + 0x10);   // 967635 stride 1
//   } else {
//       return *(byte *)(param_1 + 0x38);                // 967638 scalar byte
//   }
// These are BOOL/byte values, so the index formula uses stride 1 (index, NOT
// index*4); std::vector<bool> indexing reproduces that exactly. NO RGRandom draw.
bool GunMultiBullet::GetCanThrough(const std::vector<bool> &canThroughOverride,
                                   std::size_t bulletCount, std::size_t index,
                                   bool canThroughScalar) {
    if (UsesOverride(canThroughOverride.size(), bulletCount, index)) {
        return canThroughOverride[index];
    }
    return canThroughScalar;
}

// FAITHFUL: GunMultiBullet__GetCritics @ game_full.c:967645-967674.
//   if (*(critArray 0x78 + 0xc) == *(bulletCount 0x6c + 0xc)) {
//       // bounds check 967665: *(critArray + 0xc) <= index -> throw
//       return *(int *)(critArray + index*4 + 0x10);     // 967669
//   } else {
//       return *(int *)(param_1 + 0x2c);                 // 967672 scalar
//   }
// The override element is a raw int (crit value), index*4 stride. NO RGRandom
// draw.
int GunMultiBullet::GetCritics(const std::vector<int> &critOverride,
                               std::size_t bulletCount, std::size_t index,
                               int critScalar) {
    if (UsesOverride(critOverride.size(), bulletCount, index)) {
        return critOverride[index];
    }
    return critScalar;
}

} // namespace Game
