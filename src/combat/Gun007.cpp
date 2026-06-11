#include "combat/Gun007.hpp"

#include <cmath>

namespace Game {

// FAITHFUL: Gun007__Attack @ game_full.c:317110 --
//   (float)param_1[0x26] / (float)param_1[0x27].
// a_time / max_time. The decomp applies no clamp; an over-hold yields a ratio
// > 1 (the Min(1,...) clamp only appears later in the bullet-count formula). A
// non-positive divisor is impossible in the original (it would divide by zero);
// we return 0 to keep the brain total rather than fabricate a ratio. NO RNG draw.
float Gun007::ChargeRatio(float aTime, float maxTime) {
    if (maxTime <= 0.0F) {
        return 0.0F;
    }
    return aTime / maxTime;
}

// FAITHFUL: Gun007__Attack @ game_full.c:317119 -- `if (1.0 <= charge)`.
// NOTE: line 317117 is UnityEngine_Object__Destroy (OWNER); the gate is at 317119.
bool Gun007::IsFullCharge(float charge) {
    return kFullChargeRatio <= charge;
}

// FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317246-317264 --
//
// STEP 1 (317248-317252): primary formula stored at iterator+0xc:
//   fVar3       = Mathf.Min(1.0, charge);
//   floor       = Mathf.FloorToInt(maxCount * fVar3);
//   bulletCount = Mathf.Max(1, floor);
//
// STEP 2 (317257-317264): unconditional second write when owner+0x84 == 0
// (i.e. maxCount == 0) that overrides the limit field:
//   if (*(int *)(*(int *)(param_1 + 0x4c) + 0x84) == 0) {
//       // ARM FPSCR sign-bit: (charge < 1.0) sets bit 31; the stored value
//       // is 1 when (sign_bit == NaN_bit), which simplifies to charge >= 1.0 -> 1,
//       // else 0. The Max(1,...) result is overwritten.
//       *(param_1 + 0xc) = (charge < 1.0F) ? 0 : 1;
//   }
// When maxCount == 0 and charge < 1.0 the decomp produces 0, not 1.
// When maxCount == 0 and charge >= 1.0 the decomp produces 1.
// The owner pointer dereference is an owner concern; the condition is
// equivalent to maxCount == 0 (owner+0x84 is the maxCount int). NO RNG draw.
int Gun007::BulletCount(int maxCount, float charge) {
    // Step 1: primary Max(1, FloorToInt(maxCount * Min(1, charge))).
    const float capped =
        charge < kBulletCountChargeCap ? charge : kBulletCountChargeCap;
    const int floored =
        static_cast<int>(std::floor(static_cast<float>(maxCount) * capped));
    int limit = floored < kMinBulletCount ? kMinBulletCount : floored;

    // Step 2 (317257-317264): second unconditional gate overrides limit when
    // maxCount == 0 (owner+0x84 == 0). The ARM expression resolves to:
    //   limit = (charge >= 1.0F) ? 1 : 0;
    // This means maxCount==0 + charge < 1.0 -> limit 0 (not 1 from step 1).
    if (maxCount == 0) {
        limit = (charge >= kBulletCountChargeCap) ? 1 : 0;
    }
    return limit;
}

// FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317274 / 317281 --
//   *(param+0x10) = *(owner+0x20) + (int)(charge * *(owner+0x78));  // x
//   *(param+0x14) = *(owner+0x2c) + (int)(charge * *(owner+0x7c));  // y
// base + charge*dir per axis. The decomp truncates the scaled term to int when
// it lands in an int field; we keep the pure float math (the truncation is an
// owner storage concern, and the z/angle path at +0x18 is the same form). NO RNG.
float Gun007::MuzzleOffset(float base, float dir, float charge) {
    return base + charge * dir;
}

// FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317320 --
//   *(param+0x20) = *(owner+0x90) + (*(owner+0x94) - *(owner+0x90)) * charge.
// Unclamped lerp start..end by charge; an over-charge interpolates past `end`.
// NO RNG draw.
float Gun007::SizeForCharge(float startSize, float endSize, float charge) {
    return startSize + (endSize - startSize) * charge;
}

// FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317314 --
//   bVar1 = 0.6 < *(param+8).
// The strong-shot flag (iterator+0x1c) in the owner's alt-spawn branch
// (owner+0x84 != 0). The sibling branch (owner+0x84 == 0) instead copies an owner
// bool (owner+0x38) and is OWNER-driven, so it is not modeled. NO RNG draw.
bool Gun007::IsStrongShot(float charge) {
    return kStrongShotThreshold < charge;
}

// FAITHFUL: Gun007_<>c__Iterator0__MoveNext @ game_full.c:317189.
//   uVar4 = *(param+0x58); *(param+0x58) = -1;
//   iVar2 = (uVar4 < 3) ? uVar4 + 3 : 0;   // state 0->3, 1->4, 2->5
//   if (iVar2 != 3) { ... iVar2==5 / iVar2==4 ... return 0; }
//   /* iVar2 == 3 setup falls through */
// We model the observable lifecycle (setup / fired tick / cleanup tick / end).
bool Gun007BurstIterator::MoveNext() {
    const int entryState = m_State;
    m_State = kDone; // *(param+0x58) = -1 at entry (317190).
    m_Fired = false;

    // Remap matching the decomp: state < 3 -> state + 3, else 0.
    const int mapped = entryState < 3 ? entryState + 3 : 0;

    if (mapped == 3) {
        // SETUP (state 0): the fall-through path computes bulletCount/muzzle/size/
        // flag (modeled by Gun007's static helpers), then the decomp continues to
        // get_transform + WaitForSeconds (OWNER) and yields. No bullet here.
        m_State = kBurst;
        return true;
    }

    if (mapped == 4) {
        // BURST TICK (state 1): 317206-317207 counter(0x28)++;
        // 317209 if counter < limit(0xc) -> spawn one bullet (get_transform on
        // the muzzle prefab, OWNER) then fall through to cleanup+WaitForSeconds;
        // the counter>=limit path skips the spawn but runs the same cleanup.
        // In BOTH cases the coroutine yields (WaitForSeconds) and must re-enter.
        // The fired path re-stages state 1 (more bullets remain); the cleanup
        // path (counter >= limit) advances to state 2 (kEnd) so the next
        // MoveNext reaches TurnActivate and ends -- matching the C# coroutine
        // progression that Ghidra elides from the ARM state-write pairs.
        m_Counter += 1;
        if (m_Counter < m_Limit) {
            m_Fired = true; // one bullet spawned on this pump
            m_State = kBurst; // re-stage: more bullets remain
        } else {
            // cleanup tick: Destroy + clear owner+0x74/+0x98 (OWNER),
            // then yield once more and advance to END.
            m_State = kEnd;
        }
        return true;
    }

    if (mapped == 5) {
        // END (state 2): 317200 RGWeapon.TurnActivate(owner) (OWNER), then ends.
        return false;
    }

    // kDone / any other mapped value -> ended.
    return false;
}

} // namespace Game
