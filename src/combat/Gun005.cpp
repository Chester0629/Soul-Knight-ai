#include "combat/Gun005.hpp"

namespace Game {

// FAITHFUL: Gun005___ctor @ game_full.c:316557. maxCharge (this+0x90) is the
// float immediate 0x40000000 == 2.0f; we accept it as a parameter so the brain
// can be exercised with any weapon's configured threshold, defaulting to 2.0f.
// A non-positive divisor is impossible in the original (the ratio at 316772
// would be a divide-by-zero), so we fall back to the seeded default.
Gun005::Gun005(float maxCharge)
    : m_MaxCharge(maxCharge > 0.0F ? maxCharge : kDefaultMaxCharge) {}

void Gun005::AddCharge(float amount) {
    m_Charge += amount;
    if (m_Charge < 0.0F) {
        m_Charge = 0.0F; // charge can never be negative
    }
}

void Gun005::SetCharge(float charge) {
    m_Charge = charge < 0.0F ? 0.0F : charge;
}

// FAITHFUL: Gun005__Attack @ game_full.c:316772 --
//   fVar2 = *(float *)(param_1 + 0x8c) / *(float *)(param_1 + 0x90);
// The decomp applies NO clamp: an over-charge (charge > maxCharge) yields a
// ratio > 1, exactly as the original division produces. m_MaxCharge is
// guaranteed > 0 by the ctor, so the divisor is always well-defined.
float Gun005::ChargeRatio() const {
    return m_Charge / m_MaxCharge;
}

// FAITHFUL: Gun005__Attack @ game_full.c:316757. The aim direction is read from
// this+0x80 (x), this+0x84 (y), this+0x88 (z) and each component is scaled by
// the charge ratio fVar2; the decomp hands (fVar2*x, _, fVar2*z) -- with the
// y-axis carried through the same scale -- to the spawned RGBullet as its
// velocity (the GetComponent<RGBullet> tail at 316787 is truncated / OWNER). We
// return the full ratio-scaled vector; the bullet spawn is the owner's concern.
glm::vec3 Gun005::ScaleAim(const glm::vec3 &aimDir) const {
    const float ratio = ChargeRatio();
    return glm::vec3(ratio * aimDir.x, ratio * aimDir.y, ratio * aimDir.z);
}

// FAITHFUL: Gun005__StopWeapon @ game_full.c:316816 -- *(undefined4 *)(param_1 +
// 0x8c) = 0. After the in-fire Animator bool is cleared and the charge VFX child
// is destroyed (both OWNER), the accumulated charge is reset to 0 for the next
// pull. We model only that reset.
void Gun005::ConsumeCharge() {
    m_Charge = 0.0F;
}

} // namespace Game
