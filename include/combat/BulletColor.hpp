#ifndef GAME_BULLET_COLOR_HPP
#define GAME_BULLET_COLOR_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BulletColor
 * @brief Faithful, engine-free brain for the "BulletColor" projectile component
 *        (an RGBullet subclass that randomises its tint on spawn and only rotates
 *        while alive).
 *
 * Per-content port. BulletColor has exactly two recoverable bodies and both are
 * mostly owner-side. The ONLY pure, deterministic, unit-testable math is:
 *   (1) Start: a single UnityEngine.Random.Range(0, 6) roll that selects one of
 *       six colour/sprite variants; and
 *   (2) FixedUpdate: a per-frame gate predicate that decides whether the owner's
 *       transform-rotate runs this tick.
 * Everything else (the per-variant SpriteRenderer/Transform write, the rotation
 * itself) is the owning entity's job and is referenced in comments only.
 *
 * WHAT THE DECOMP SHOWS (no reconstruction of unseen logic):
 *
 *  - BulletColor__Start (@ game_full.c:962850):
 *        if (class_init_flag) {                       // IL2CPP static-init guard
 *            value = UnityEngine.Random.Range(0, 6);  // one int draw, {0..5}
 *            switch (value) { case 0..5: get_transform(this); }  // owner write
 *        }
 *    The Range(0, 6) draw is the GLOBAL Unity RNG (max EXCLUSIVE), modelled here
 *    by the injected RGRandom (same Xorshift128, same int semantics), exactly as
 *    BossAI06Child models its global Range(0, 2). Every switch case collapses to
 *    the same get_transform(this) tail (a SpriteRenderer/Transform colour write);
 *    the per-index colour VALUE is not recoverable from the decomp (the jumptable
 *    targets are identical "does not return" stubs), so we model the chosen
 *    INDEX only -- not which colour it maps to. NO clamp, NO second draw.
 *
 *  - BulletColor__FixedUpdate (@ game_full.c:962903):
 *        alive = (this+0x20 != 0);                    // RGBullet.awake @0x20
 *        rot   = alive ? *(this+0x1c) : 0;            // RGBullet.rotate_angle @0x1C
 *        if (!alive || rot == 0) return;              // gate: skip rotate
 *        get_transform(this);                         // owner: rotate transform
 *    The recoverable scalar is the gate predicate `alive && rotateAngle != 0`;
 *    the transform rotation itself is owner-side. NO RGRandom draw.
 *
 * Field offsets are the RGBullet base layout (see recreation/Weapon/RGBullet.cs):
 *   0x1C rotate_angle (int)    0x20 awake (bool).
 *
 * Determinism: Start makes exactly ONE int draw; FixedUpdate makes ZERO draws.
 * A same-seeded RGRandom replays the colour-index roll bit-for-bit.
 *
 * @see recreation/Weapon/RGBullet.cs (base field map: rotate_angle @0x1C,
 *      awake @0x20); FAITHFUL: BulletColor @ game_full.c:962850, 962903.
 */
class BulletColor {
public:
    /// Start's colour/sprite selector ceiling: UnityEngine.Random.Range(0, 6),
    /// max EXCLUSIVE -> yields one of {0, 1, 2, 3, 4, 5}.
    static constexpr int kColorCount = 6;

    BulletColor() = default;

    /// Seed the deterministic selector stream (models the global Unity RNG).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- Field accessors (only fields the decomp actually reads/writes) ----

    /// RGBullet.awake (0x20): the alive flag FixedUpdate gates on.
    bool Awake() const { return m_Awake; }
    void SetAwake(bool v) { m_Awake = v; }

    /// RGBullet.rotate_angle (0x1C): per-frame spin amount FixedUpdate gates on.
    int RotateAngle() const { return m_RotateAngle; }
    void SetRotateAngle(int v) { m_RotateAngle = v; }

    /// Last colour index selected by PickColorIndex(), or -1 before the first roll.
    int ColorIndex() const { return m_ColorIndex; }

    // ---- Start: the colour/sprite selector roll ----------------------------

    /**
     * @brief FAITHFUL: BulletColor__Start -> the one decision in the class.
     *
     * Draws UnityEngine.Random.Range(0, kColorCount) (one int, max EXCLUSIVE) to
     * pick one of six colour/sprite variants, stores it (ColorIndex), and returns
     * it. The decomp's static-init guard is IL2CPP class-init (always true at
     * runtime, not a gameplay gate), so the draw is always taken -- mirroring the
     * original, which always rolls on spawn. The per-index SpriteRenderer/Transform
     * colour write (the switch body, owner-side) is NOT modelled: only the chosen
     * index is recoverable.
     *
     * @return the selected colour index in [0, kColorCount).
     */
    int PickColorIndex();

    // ---- FixedUpdate: the per-frame rotate gate ----------------------------

    /**
     * @brief FAITHFUL: BulletColor__FixedUpdate gate predicate (no RNG).
     *
     * Returns true iff the owner's transform-rotate should run this tick:
     * `awake (0x20) != 0 && rotate_angle (0x1C) != 0`. When false the decomp's
     * body returns early (no rotation). The rotation itself (get_transform) is
     * owner-side and is not modelled here.
     *
     * @param awake       the RGBullet.awake flag (0x20).
     * @param rotateAngle the RGBullet.rotate_angle field (0x1C).
     * @return true if the transform should rotate this frame; false otherwise.
     */
    static bool ShouldRotate(bool awake, int rotateAngle);

    /**
     * @brief Member overload of ShouldRotate using this instance's awake /
     *        rotate_angle fields. Convenience for owner FixedUpdate wiring.
     */
    bool ShouldRotate() const { return ShouldRotate(m_Awake, m_RotateAngle); }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};            ///< deterministic stream; one draw in PickColorIndex.

    // Only fields BulletColor bodies actually read/write are modelled here.
    int m_RotateAngle = 0;       ///< 0x1C rotate_angle (FixedUpdate gate input).
    bool m_Awake = false;        ///< 0x20 awake (FixedUpdate gate input).
    int m_ColorIndex = -1;       ///< last PickColorIndex() result (-1 = not rolled).
};

} // namespace Game

#endif /* GAME_BULLET_COLOR_HPP */
