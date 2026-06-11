#include "combat/GunThrow.hpp"

#include <cmath>

namespace Game {

namespace {

// Mathf.CeilToInt(x): round toward +inf. The decomp computes CeilToInt(f * 0.5)
// in several places; we keep the *0.5 at the call site to mirror the decomp.
int CeilToInt(float value) {
    return static_cast<int>(std::ceil(value));
}

// The decomp's `(n * 2 & 2) - 1` idiom: extracts bit 1 of n, scaled to +-1.
// (n*2 & 2) == 2 when bit0 of n is set, else 0; minus 1 -> +1 / -1.
int OddBitSign(int n) {
    return ((n * 2) & 2) - 1;
}

} // namespace

// FAITHFUL: GunThrow__GetShootAngle @ game_full.c:969528 count cascade
// (969559-969570).
float GunThrow::SpreadSpanForCount(int bulletCount) {
    if (bulletCount < kMinSpreadCount) {
        return 0.0F; // 969551: count < 2 -> Vector3.zero (z == 0)
    }
    if (bulletCount < 4) {
        return kSpread2to3; // 969560: fVar12 = 15
    }
    if (bulletCount < 6) {
        return kSpread4to5; // 969563: fVar12 = 30
    }
    if (bulletCount < 8) {
        // 969566: pfVar6 = &DAT_00b24804 -> *pfVar6 (UNRECOVERABLE).
        return kSpread6to7;
    }
    // 969565: 7 < count -> pfVar6 = &DAT_00b24808 -> *pfVar6 (UNRECOVERABLE).
    return kSpread8plus;
}

// FAITHFUL: GunThrow__GetShootAngle @ game_full.c:969528. NO RGRandom draw:
// the spread is a deterministic geometric fan-out by count + index.
float GunThrow::GetShootAngle(int bulletCount, int bulletIndex,
                              float baseAngleOffset) {
    if (bulletCount < kMinSpreadCount) {
        return 0.0F; // 969551: Vector3.zero early-out
    }

    const float span = SpreadSpanForCount(bulletCount); // fVar12
    // baseAngleOffset is param_2+0x30 (decomp fVar9), summed under the abs.

    int signedStep = 0; // iVar3 -> fVar11 (numerator)
    int halfSteps = 0;  // iVar4 (pre-sign) ; halfSteps * stepSign -> fVar10
    int stepSign = 0;   // iVar8

    if (bulletCount % 2 == 1) {
        // 969585-969605: odd count branch. index drives a +-CeilToInt(index/2).
        signedStep = CeilToInt(static_cast<float>(bulletIndex) * 0.5F) *
                     ((bulletIndex & 1) * 2 - 1);
        halfSteps = CeilToInt(static_cast<float>(bulletCount - 1) * 0.5F);
        stepSign = OddBitSign(bulletCount - 1);
    } else {
        // 969611-969631: even count branch. index uses (index+1) and a fixed
        // -1 step sign (uVar7 is even here, so iVar8 = -1).
        signedStep = CeilToInt(static_cast<float>(bulletIndex + 1) * 0.5F) *
                     OddBitSign(bulletIndex + 1);
        halfSteps = CeilToInt(static_cast<float>(bulletCount) * 0.5F);
        // iVar8 = (count even) ? -1 : 1; count is even on this branch.
        stepSign = (bulletCount == (bulletCount & ~1)) ? -1 : 1;
    }

    // 969636-969640: fVar10 = Abs(iVar4*iVar8); fVar11 = iVar3;
    // z = ABS(span + offset) * (fVar11 / fVar10).
    const float denom = std::fabs(static_cast<float>(halfSteps * stepSign));
    const float numer = static_cast<float>(signedStep);
    return std::fabs(span + baseAngleOffset) * (numer / denom);
}

// FAITHFUL: GunThrow__GetWeaponAngle @ game_full.c:969374. NO RGRandom draw.
float GunThrow::GetWeaponAngle(int bulletCount, int siblingCount, int index) {
    if (bulletCount < kMinSpreadCount) {
        return 0.0F; // 969390: count < 2 -> Vector3.zero
    }

    // 969402: fVar4 = FloorToInt(count * 0.5) -> Abs(...); used as half-span.
    const int half = std::abs(static_cast<int>(
        std::floor(static_cast<float>(bulletCount) * 0.5F)));

    // 969405-969407: span = (siblingCount == 0) ? 60 : siblingCount.
    float span = static_cast<float>(siblingCount);
    if (siblingCount == 0) {
        span = kWeaponAngleDefaultSpan;
    }

    // 969415-969419: idxOffset = index - half; even count -> + 0.5 bias.
    float idxOffset = static_cast<float>(index) - static_cast<float>(half);
    if (bulletCount % 2 != 1) {
        idxOffset = idxOffset + 0.5F;
    }

    // 969420: z = span * (idxOffset / half) * 0.5.
    return span * (idxOffset / static_cast<float>(half)) * 0.5F;
}

// FAITHFUL: GunThrow__ResetConsume @ game_full.c:969427 (969445-969452).
int GunThrow::ResetConsume(int damage, int count, int bulletCount) {
    if (bulletCount == 0) {
        return 0; // guard the decomp's fVar6 / fVar5 division by 0x6c field
    }
    // consume = CeilToInt((damage * count) / bulletCount).
    const float numer = static_cast<float>(damage * count);
    const float denom = static_cast<float>(bulletCount);
    return CeilToInt(numer / denom);
}

// FAITHFUL: GunThrow__ResetDir @ game_full.c:969505: `if (childCount < 1) return`.
bool GunThrow::ResetDirHasChild(int childCount) {
    return childCount > 0;
}

// FAITHFUL: GunThrow_<Throwing>c__Iterator0__MoveNext @ game_full.c:969650.
bool GunThrowThrowingIterator::MoveNext(int childCount) {
    const int state = m_State;     // 969660: uVar1 = *(param_1 + 0x14)
    m_State = kDone;               // 969661: *(param_1 + 0x14) = -1
    m_Fired = false;
    // 969662: `if ((uVar1 | 1) == 1)` -> only state 0 or 1 proceed.
    if ((state | 1) == 1) {
        // 969668: owner op_Implicit(weapon); 969674: childCount > 0 -> throw the
        // last muzzle child. We model the predicate; the GetChild/get_gameObject
        // is the owner's side-effect.
        if (childCount > 0) {
            m_Fired = true;
        }
        m_State = kDone; // 969694: *(param_1 + 0x14) = -1 (terminal)
    }
    return false; // MoveNext always returns 0
}

} // namespace Game
