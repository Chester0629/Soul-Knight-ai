#ifndef GAME_GUN018_HPP
#define GAME_GUN018_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun018
 * @brief Faithful pellet-config / per-pellet scatter math for the "Gun018"
 *        shotgun-burst weapon (Soul Knight 1.7.10).
 *
 * Per-content port. Gun018 is a multi-pellet weapon: it fires a fixed number of
 * pellets per pull, each scattered around the aim by a SYMMETRIC random spread.
 * Unlike the geometric-fan guns (GunThrow), Gun018's scatter is drawn from the
 * weapon's RGRandom stream, so the draw COUNT and ORDER are observable and must
 * be preserved frame-for-frame. The bullet Instantiate / GetComponent<RGBullet>
 * / muzzle Transform.get_position spawn, the owner-object virtual call at
 * (*piVar2 + 0xf4), and the truncated get_position tail are all OWNER concerns;
 * this brain models only the recoverable scalar/RNG math that FEEDS them:
 *
 *   - ctor (965246): the pellet config immediates --
 *       bullet_1_count     (this+0x8c) = 3
 *       bullet_1_speed     (this+0x90) = 20.0f
 *       bullet_1_angle     (this+0x98) = 0x14 (20)
 *       bullet_1_deviation (this+0x9c) = 0x14 (20)
 *     (plus two base RGWeapon immediates at 0x78=2.0f and 0x80=1.0f).
 *   - Attack (965270): spread = baseAngle(this+0x30, via VectorSignedToFloat)
 *     + recoil(ownerObj+0x20) -- ADDITIVE, distinct from the multiplicative
 *     scatter of the single-shot guns -- then ONE RGRandom::Range(float)
 *     draw of (-spread, +spread). (The owner object, its 0xf4 virtual, and the
 *     get_position spawn tail are OWNER.)
 *   - CreateBullet (965312): a per-pellet scatter. A non-negative-counter gate
 *     (-c <= c, i.e. c >= 0 on field 0x94) guards ONE RGRandom::Range(int) draw
 *     of (-deviation, +deviation) where deviation is the int field 0x9c.
 *
 * RNG faithfulness: Attack draws ONE float (Range max-INCLUSIVE); CreateBullet
 * draws ONE int (Range max-EXCLUSIVE) -- and ONLY when its counter gate passes.
 * Both distinctions are preserved here. The brain owns no Vector3/Quaternion;
 * the spread is a scalar euler-Z magnitude the owner feeds to Quaternion.Euler.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      FAITHFUL: Gun018 @ game_full.c:965246-965327.
 */
class Gun018 {
public:
    // ---- ctor immediates (965252-965259), all directly recoverable ----------

    /// Pellets fired per pull. FAITHFUL: 965254 this+0x8c = 3.
    static constexpr int kBulletCount = 3;
    /// Pellet launch speed. FAITHFUL: 965255 this+0x90 = 0x41a00000 == 20.0f.
    static constexpr float kBulletSpeed = 20.0F;
    /// Pellet base angle field. FAITHFUL: 965256 this+0x98 = 0x14 == 20.
    static constexpr int kBulletAngle = 20;
    /// Pellet per-pellet deviation field. FAITHFUL: 965257 this+0x9c = 0x14 == 20.
    static constexpr int kBulletDeviation = 20;

    /// Base RGWeapon immediate. FAITHFUL: 965252 this+0x78 = 0x40000000 == 2.0f.
    static constexpr float kBaseField0x78 = 2.0F;
    /// Base RGWeapon immediate. FAITHFUL: 965253 this+0x80 = 0x3f800000 == 1.0f.
    static constexpr float kBaseField0x80 = 1.0F;

    Gun018() = default;

    /// Seed the weapon's deterministic stream (call once at spawn). Gun018's
    /// scatter is RNG-driven, so this stream IS observed by Attack/CreateBullet.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    int BulletCount() const { return kBulletCount; }
    float BulletSpeed() const { return kBulletSpeed; }
    int BulletDeviation() const { return kBulletDeviation; }

    /**
     * @brief The ADDITIVE spread half-angle used by Attack's scatter draw.
     *
     * FAITHFUL: Gun018__Attack @ game_full.c:965296-965297 --
     *   fVar4 = VectorSignedToFloat(this+0x30);     // base aim angle
     *   fVar4 = fVar4 + *(float *)(ownerObj + 0x20); // + recoil  (ADDITIVE)
     * The owner object is *(this+0x50) and `recoil` is read from its +0x20 after
     * the virtual call at (*owner + 0xf4) (OWNER). We take baseAngle and recoil
     * as scalars and return their sum -- the symmetric half-span fed to the
     * Range(float) draw. NO RGRandom draw happens in THIS computation.
     */
    static float AttackSpread(float baseAngle, float recoil);

    /**
     * @brief Perform Attack's single symmetric scatter draw, in degrees.
     *
     * FAITHFUL: Gun018__Attack @ game_full.c:965302 --
     *   RGRandom::Range(this+0x60, -fVar4, fVar4, 0)
     * spread = AttackSpread(baseAngle, recoil); draws ONE float in
     * [-spread, +spread] (Range float, max-INCLUSIVE). The muzzle
     * get_position(this+0x4c) spawn that consumes the result is OWNER (and is
     * the truncated tail of the body). Advances the stream by exactly one float.
     * @return the scattered euler-Z offset (degrees) for this pull.
     */
    float AttackScatter(float baseAngle, float recoil);

    /**
     * @brief CreateBullet's per-pellet gate: fire iff the counter is >= 0.
     *
     * FAITHFUL: Gun018__CreateBullet @ game_full.c:965319 --
     *   if (-*(int *)(this+0x94) <= *(int *)(this+0x94)) { ... }
     * The predicate `-c <= c` is true exactly when c >= 0. Field 0x94 is a
     * runtime counter NOT initialised by the ctor; the owner advances it. We
     * model only the predicate.
     * @param counter the owner's pellet counter (field 0x94).
     */
    static bool PelletGatePasses(int counter);

    /**
     * @brief Perform CreateBullet's per-pellet scatter draw (int), in degrees.
     *
     * FAITHFUL: Gun018__CreateBullet @ game_full.c:965324 --
     *   RGRandom::Range(this+0x60, -*(int *)(this+0x9c), *(int *)(this+0x9c), 0)
     * Draws ONE int in [-deviation, +deviation) (Range int, max-EXCLUSIVE) using
     * the int field 0x9c (kBulletDeviation). The bullet Instantiate that
     * consumes the angle is OWNER. The draw happens ONLY when PelletGatePasses;
     * when gated off this advances the stream by ZERO draws and returns 0.
     * @param counter the owner's pellet counter (field 0x94).
     * @return the scattered integer angle offset, or 0 when the gate is closed.
     */
    int CreateBulletScatter(int counter);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
};

} // namespace Game

#endif /* GAME_GUN018_HPP */
