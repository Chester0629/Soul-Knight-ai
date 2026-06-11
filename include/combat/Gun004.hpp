#ifndef GAME_GUN004_HPP
#define GAME_GUN004_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun004
 * @brief Faithful per-shot scatter + burst-gate logic for the "Gun004" weapon
 *        (Soul Knight 1.7.10).
 *
 * Gun004 is a BURST gun: a single Attack() fires a short, fixed-size salvo of
 * single bullets in a tight loop. The decomp splits this into two bodies:
 *
 *   - the burst driver Gun004.Attack (emitted as the stripped helper
 *     FUN_003ed07c @ game_full.c:316285): a loop that, while a shot counter is
 *     below the configured burst size (`count`, the int field at owner+0x6c)
 *     and an interrupt flag (the byte at owner+0x70) is clear, calls
 *     CreateBullet() once per shot; when the counter reaches the limit it
 *     stops the muzzle animation (vtable+0x134) and plays an end-of-burst SFX.
 *
 *   - the per-shot spawn Gun004.CreateBullet (@ game_full.c:316341): the
 *     canonical single-shot gun scatter shared with Gun001/Gun005 -- a base
 *     spread angle plus recoil, then ONE symmetric RGRandom float draw.
 *
 * This brain models ONLY the recoverable scalar/state math that FEEDS the
 * side-effects. The muzzle Animator bool (FUN_003ecbdc / vtable+0x134), the
 * RGMusicManager.PlayEffect end-SFX, the PrefabPool bullet Instantiate /
 * GetComponent<RGBullet> spawn, MonoBehaviour.Invoke, and the Singleton<>.Inst
 * pool access are all OWNER concerns. The burst is modelled as a counter the
 * owner re-invokes: ShouldFireShot() reports whether the next pump fires a
 * bullet or ends the burst, and the owner spawns the bullet and (on each fired
 * shot) calls ShotScatterAngle to keep the RNG stream in lockstep.
 *
 * @see metadata Gun004.cs (field names: count / has_delay / max_delay);
 *      FAITHFUL: Gun004 @ game_full.c:316285 (Attack driver) and 316341
 *      (CreateBullet). Field offsets are decoded from the decomp's own
 *      self-consistent accesses (shared with Gun001__Attack @ 315755).
 */
class Gun004 {
public:
    /**
     * @param burstCount the configured burst size (owner field `count` at
     *        owner+0x6c, read as unaff_r4[0x1b] by the driver). A non-positive
     *        burst is treated as zero shots (the loop's first compare ends it).
     */
    explicit Gun004(int burstCount) : m_BurstCount(burstCount) {}

    /// Seed the weapon's deterministic stream (owner+0x60). One float is drawn
    /// per fired shot (see ShotScatterAngle); keep it in lockstep.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// The configured burst size (`count`, owner+0x6c). The loop ends when the
    /// fired-shot counter reaches this value.
    int BurstCount() const { return m_BurstCount; }

    /// Number of shots fired so far in the current burst (the driver's local
    /// loop counter unaff_r5). Reset by BeginBurst().
    int ShotsFired() const { return m_ShotsFired; }

    /**
     * @brief Start a fresh burst: reset the fired-shot counter to 0.
     *
     * FAITHFUL: Gun004.Attack @ game_full.c:316285. The driver's loop counter
     * (unaff_r5) is incremented before each limit compare; the decompiler could
     * not recover its initial register value, but the source semantics of a
     * salvo loop start it at 0 (see fabrication_flags: kInitialShotCounter).
     */
    void BeginBurst() { m_ShotsFired = 0; }

    /**
     * @brief Advance the burst one pump: decide whether this pump fires a bullet
     *        or the burst ends, and update the shot counter.
     *
     * FAITHFUL: Gun004.Attack @ game_full.c:316295-316309. Mirrors the inner
     * loop body exactly, in order:
     *   1. unaff_r5 = unaff_r5 + 1               (count this pump's shot)
     *   2. if (count(0x6c) <= unaff_r5) -> END   (stop anim + end-SFX; OWNER)
     *   3. if ((char)flag(0x70) != 0)   -> END   (in-attack interrupt break)
     *   4. else -> Gun004.CreateBullet()         (FIRE one bullet; OWNER spawn)
     *
     * @param interrupted the byte flag at owner+0x70 (unaff_r4[0x1c]); when
     *        non-zero the burst breaks immediately after the count compare.
     * @return true iff this pump fires a bullet (the owner spawns one and must
     *         draw one scatter angle via ShotScatterAngle); false when the burst
     *         has ended (limit reached or interrupted) -- the owner then plays
     *         the end-of-burst SFX (OWNER) and stops re-invoking.
     */
    bool ShouldFireShot(bool interrupted);

    /**
     * @brief Per-shot spread magnitude (degrees) fed to the scatter draw:
     *        base + base*recoil.
     *
     * FAITHFUL: Gun004.CreateBullet @ game_full.c:316365-316366. `base` is the
     * weapon's base spread angle (owner+0x30, decomp uVar3 via
     * VectorSignedToFloat); `recoil` is a float read off the bullet/RGBullet
     * component (component+0x20) fetched through the gameObject at owner+0x50 --
     * an OWNER-supplied multiplier. The decomp computes fVar4 = base + base*recoil.
     * NO RGRandom draw here.
     * @return the half-span magnitude used as the +/- bound below.
     */
    static float ShotSpread(float base, float recoil);

    /**
     * @brief One fired shot's scatter angle: Range(-spread, +spread).
     *
     * FAITHFUL: Gun004.CreateBullet @ game_full.c:316371
     * (RGRandom__Range(owner+0x60, -fVar4, fVar4)). Draws EXACTLY ONE float from
     * this weapon's stream (max-INCLUSIVE float Range), matching the decomp's
     * single symmetric draw per spawned bullet. `spread` is from ShotSpread.
     * @return the bullet's deterministic scatter angle within [-spread, +spread].
     */
    float ShotScatterAngle(float spread);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    int m_BurstCount = 0; ///< owner+0x6c (`count`); driver's unaff_r4[0x1b].
    int m_ShotsFired = 0; ///< driver's local loop counter unaff_r5.
};

} // namespace Game

#endif /* GAME_GUN004_HPP */
