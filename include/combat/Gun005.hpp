#ifndef GAME_GUN005_HPP
#define GAME_GUN005_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun005
 * @brief Faithful charge-ratio scalar math for the "Gun005" charge weapon
 *        (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. Gun005 is a CHARGE weapon: while the trigger is held the
 * weapon builds charge, and on release the spawned bullet's velocity is the aim
 * direction scaled by the charge ratio (a partial charge fires a slower/weaker
 * shot, a full charge fires at full direction magnitude). The whole body is
 * heavily truncated -- bullet Instantiate / GetComponent<RGBullet> spawn,
 * Animator GetBool/SetBool, the muzzle Transform walks and the Object.Destroy of
 * the charge VFX are all OWNER concerns. This brain models ONLY the recoverable
 * scalar/state math that FEEDS the spawn:
 *
 *   - Attack (Gun005__Attack @ game_full.c:316757): the charge ratio
 *       fVar2 = charge(this+0x8c) / maxCharge(this+0x90)
 *     and the per-axis scaling of the aim direction (this+0x80/0x84/0x88) by
 *     that ratio -- the velocity vector handed to the spawned RGBullet
 *     (the GetComponent<RGBullet> tail is truncated / OWNER).
 *   - StopWeapon (Gun005__StopWeapon @ game_full.c:316795): after the Animator
 *     "in-fire" bool is cleared and the charge VFX child is destroyed, charge
 *     (this+0x8c) is reset to 0 -- modeled by ConsumeCharge().
 *   - _ctor (Gun005___ctor @ game_full.c:316557): maxCharge (this+0x90) is the
 *     float immediate 0x40000000 == 2.0f. The other ctor writes at 0x80/0x84/0x88
 *     are small integer constants (5/50/10), not the live aim vector -- the
 *     owner's aiming overwrites 0x80-0x88 with the real float aim direction
 *     before Attack runs (Attack reads them through the ARM vcvt that Ghidra
 *     surfaces as VectorSignedToFloat), so they are NOT modeled as defaults here.
 *   - SetAttack (Gun005__SetAttack @ game_full.c:316601) and Awake
 *     (Gun005__Awake @ game_full.c:316579) carry no recoverable pure scalar
 *     logic: SetAttack is an Animator GetBool/SetBool ("in-fire") toggle gated on
 *     this+0x14 / ammo (this+0x34) and Awake is a truncated get_gameObject tail
 *     -- both OWNER concerns, intentionally not modeled.
 *
 * Determinism: NO Gun005 body draws from rg_random (the spread is the pure
 * geometric charge scalar, not random scatter), so this unit makes ZERO RNG
 * draws. The RGRandom member is carried only for owner-side parity/lockstep and
 * is never advanced; Seeded() lets a caller confirm the seed without a draw.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun005 @ game_full.c:316557-316830.
 */
class Gun005 {
public:
    /// Default maximum charge (the divisor in the charge ratio).
    /// FAITHFUL: Gun005___ctor @ 316566 -- this+0x90 = 0x40000000 == 2.0f.
    static constexpr float kDefaultMaxCharge = 2.0F;

    /**
     * @brief Construct a charge weapon.
     * @param maxCharge full-charge threshold (this+0x90). Values <= 0 fall back
     *        to kDefaultMaxCharge so the ratio divisor is always well-defined
     *        (the ctor seeds 2.0f; a real weapon never has a zero divisor).
     * Starts with zero accumulated charge.
     */
    explicit Gun005(float maxCharge = kDefaultMaxCharge);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// Configured full-charge threshold (this+0x90).
    float MaxCharge() const { return m_MaxCharge; }

    /// Currently accumulated charge (this+0x8c).
    float Charge() const { return m_Charge; }

    /**
     * @brief Add accumulated charge (the trigger-held build-up).
     *
     * The decomp's per-frame charge accumulation itself is an owner/animator
     * concern (it lives outside the recovered Gun005 bodies); this setter lets a
     * caller feed the charge the owner has accrued so ChargeRatio()/ScaleAim()
     * can be unit-tested. Charge is clamped to >= 0 (negative charge is not a
     * state the weapon can be in).
     */
    void AddCharge(float amount);

    /// Set the accumulated charge directly (clamped to >= 0). Owner convenience.
    void SetCharge(float charge);

    /**
     * @brief The charge ratio fed to the bullet velocity scale.
     *
     * FAITHFUL: Gun005__Attack @ 316772 -- fVar2 = charge(0x8c) / maxCharge(0x90).
     * No clamp exists in the decomp: a charge above maxCharge yields a ratio > 1
     * (an over-charged shot), exactly as the original divides. NO RGRandom draw.
     */
    float ChargeRatio() const;

    /**
     * @brief Scale an aim direction by the current charge ratio -> bullet velocity.
     *
     * FAITHFUL: Gun005__Attack @ 316757. The decomp reads the aim direction from
     * this+0x80 (x), this+0x84 (y), this+0x88 (z) and multiplies each component
     * by the charge ratio fVar2; the resulting (ratio*x, ratio*y, ratio*z) is the
     * velocity argument to the spawned RGBullet (the GetComponent<RGBullet> tail
     * is truncated / OWNER). We return that scaled vector; the spawn is the
     * owner's job. NO RGRandom draw.
     *
     * @param aimDir the live aim direction (this+0x80/0x84/0x88), set by the
     *        owner's aiming before Attack runs.
     */
    glm::vec3 ScaleAim(const glm::vec3 &aimDir) const;

    /**
     * @brief Consume the accumulated charge after a shot (charge -> 0).
     *
     * FAITHFUL: Gun005__StopWeapon @ 316816 -- after the in-fire Animator bool is
     * cleared and the charge VFX is destroyed, this+0x8c is set to 0. We model
     * only that reset; the Animator toggle / Object.Destroy are OWNER concerns.
     */
    void ConsumeCharge();

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< deterministic stream; never advanced on any path.
    float m_MaxCharge = kDefaultMaxCharge; ///< this+0x90 (charge ratio divisor).
    float m_Charge = 0.0F;                 ///< this+0x8c (accumulated charge).
};

} // namespace Game

#endif /* GAME_GUN005_HPP */
