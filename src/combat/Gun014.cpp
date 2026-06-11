#include "combat/Gun014.hpp"

namespace Game {

// FAITHFUL: Gun014__AdjustAngle @ game_full.c:964354 --
//   if (*(int *)(param_1 + 0x7c) * *(int *)(param_1 + 0x6c) < 0x169) return;
// The body re-rolls the angle ONLY on the complement of that gate, so the
// predicate that arms the (single) draw is `angle * count >= kReadjustThreshold`.
// Pure: NO RGRandom draw.
bool Gun014::ShouldReadjust(int bulletCount) const {
    return m_BaseAngle * bulletCount >= kReadjustThreshold;
}

// FAITHFUL: Gun014__AdjustAngle @ game_full.c:964350.
//   uVar1 = FUN_001ceef4(0x168); *(param_1 + 0x7c) = uVar1;
// FUN_001ceef4(0x168) is called with NO rng-instance argument; param_1+0x60
// (the RGRandom field) is never accessed anywhere in Gun014__AdjustAngle
// (964350-964361). The call is therefore NOT RGRandom::Range on the per-object
// stream. Do NOT advance m_Rng here.
// TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
bool Gun014::ReadjustAngle(int bulletCount) {
    if (!ShouldReadjust(bulletCount)) {
        return false; // 964355: early return -- angle unchanged, stream unadvanced
    }
    // 964358: FUN_001ceef4(0x168) -- global/non-per-object random; source unknown.
    // TODO[verify]: FUN_001ceef4(0x168) is not RGRandom::Range; do not advance m_Rng here.
    // m_BaseAngle is written by the owner from the result of FUN_001ceef4(0x168).
    return true;
}

// FAITHFUL: Gun014__CreateBullet @ game_full.c:964406 -- `if (count < 1) return`.
bool Gun014::CanCreateBullet(int bulletCount) {
    return bulletCount >= kMinBulletCount;
}

// FAITHFUL: Gun014__CreateBullet @ game_full.c:964421-964427.
//   uVar2 = count(0x6c);
//   if ((uVar2 & 1) == 0)  -> VectorSignedToFloat(-(int)uVar2 / 2)       [even]
//   else                   -> VectorSignedToFloat(-((int)(uVar2-1) / 2)) [odd]
// This is the lowest (most-negative) pellet offset of the symmetric fan; the
// owner walks start..start+count to lay out the pellets. Pure, NO draw.
int Gun014::FanStartIndex(int bulletCount) {
    if ((bulletCount & 1) == 0) {
        return -(bulletCount / 2);
    }
    return -((bulletCount - 1) / 2);
}

// FAITHFUL: Gun014__CreateBullet @ game_full.c:964420/964428 --
//   fVar5 = base(0x30);
//   fVar5 = fVar5 + fVar5 * recoil(recoilObj+0x20);  ->  base * (1 + recoil).
// The recoilObj is the weapon-config object fetched via the 0xf4 slot-call
// (param_1+0x50 -> +0xf4); recoil is its +0x20 float. We take both scalars as
// inputs (owner-fed) and reproduce the exact `base + base*recoil` form. NO draw.
float Gun014::SpreadWithRecoil(float baseSpread, float recoil) {
    return baseSpread + baseSpread * recoil;
}

// FAITHFUL: Gun014__CreateBullet @ game_full.c:964434 --
//   RGRandom__Range(rng(0x60), -fVar5, fVar5, 0);
// The canonical symmetric scatter: ONE float draw in [-spread, +spread]
// (Range(float) is max-inclusive). The result is the per-pellet heading jitter
// added by the owner before the PrefabPool Instantiate (truncated / OWNER tail).
float Gun014::ScatterDraw(float spreadHalfWidth) {
    return m_Rng.Range(-spreadHalfWidth, spreadHalfWidth);
}

// FAITHFUL: Gun014__Attack @ game_full.c:964373 --
//   if ((char)param_1[0x1c] == '\0') Gun014__CreateBullet(param_1);
//   else UnityEngine_MonoBehaviour__Invoke(param_1, ..., param_1[0x1d], 0);
// in_atk(0x1c) == 0 -> fire now; otherwise schedule the delayed re-fire. The
// CreateBullet call, the Invoke, the get_transform slot-call (964378) and the
// RGMusicManager.PlayEffect tail (964380-964386) are OWNER. NO RGRandom draw.
bool Gun014::FireOrInvoke(bool inAtk) {
    return !inAtk;
}

} // namespace Game
