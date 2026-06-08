#ifndef GAME_GUN019_HPP
#define GAME_GUN019_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun019
 * @brief Faithful burst-fire counter + per-shot scatter math for the "Gun019"
 *        weapon (Soul Knight 1.7.10, an RGWeapon subclass).
 *
 * Per-content port. Gun019 is a BURST gun: a single recovered body
 * (Gun019__CreateBullet) holds BOTH the burst re-schedule loop AND the
 * per-shot scatter. The body forks on a gate field (c_count, owner+0x70):
 *
 *   - BURST-RESCHEDULE branch (c_count < 0): play the fire SFX (owner), bump
 *     the burst index (owner+0x80), and if index < count (owner+0x78)
 *     re-schedule the next shot via Invoke("CreateBullet", delay (owner+0x7c));
 *     otherwise clear in_atk (owner+0x84) to stop the burst.
 *     The gate is `x < -x` which is true exactly when x < 0 (signed int).
 *   - MAIN-SHOT branch (c_count >= 0): compute the scatter half-angle
 *     spread = angle + angle*recoil  (angle is owner+0x30, an int read as a
 *     float; recoil is a bullet-component float at component+0x20 supplied by
 *     the owner), then draw ONE RGRandom float Range(-spread, +spread) for the
 *     shot's scatter, and spawn the bullet at the muzzle (owner+0x4c, OWNER).
 *
 * This brain models ONLY the recoverable scalar/state math that FEEDS those
 * side-effects. The RGMusicManager.PlayEffect, the MonoBehaviour.Invoke
 * re-schedule, the GetComponent walk, the muzzle Transform.get_position spawn
 * and the bullet Instantiate are all OWNER concerns (the body's spawn tail is
 * truncated -- "Subroutine does not return").
 *
 * Determinism: the main-shot branch draws EXACTLY ONE RGRandom float per shot
 * (the symmetric scatter Range(-spread, +spread)); the burst-reschedule branch
 * draws ZERO (it only plays a sound, bumps a counter and re-schedules). Preserve
 * the single draw per main shot and never advance the stream on the burst path.
 *
 * Field names below come from the IL2CPP metadata dump (signatures/names only;
 * bodies empty there) cross-checked against the decomp's own offsets:
 *   c_count 0x70, angle 0x30, count 0x78, delay 0x7c, index 0x80, in_atk 0x84,
 *   rng 0x60, muzzle 0x4c, sfx 0x10, owner 0x50.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference);
 *      IL2CPP Gun019.cs (field names);
 *      FAITHFUL: Gun019__CreateBullet @ game_full.c:965343.
 */
class Gun019 {
public:
    /// Idle / stopped sentinel for the in_atk flag (owner+0x84 == 0).
    static constexpr bool kInAtkStopped = false;

    Gun019() = default;

    /// Seed the weapon's deterministic stream (owner+0x60). One float is drawn
    /// per main shot (see ScatterAngle); keep it in lockstep with the owner.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief The burst-reschedule branch is taken iff c_count < 0.
     *
     * FAITHFUL: Gun019__CreateBullet @ game_full.c:965357 --
     * `if (*(int *)(param_1 + 0x70) < -*(int *)(param_1 + 0x70))`. For a signed
     * int x, `x < -x` is equivalent to `2x < 0`, i.e. x < 0; so a NEGATIVE
     * c_count selects the burst-reschedule path (SFX + index bump +
     * re-schedule/stop) and a non-negative c_count selects the main-shot path
     * (scatter + spawn).
     * @return true -> burst-reschedule branch; false -> main-shot branch.
     */
    static bool IsBurstReschedule(int cCount) { return cCount < 0; }

    /**
     * @class BurstStep
     * @brief Result of one burst-reschedule pump (the c_count < 0 branch).
     *
     * FAITHFUL: Gun019__CreateBullet @ game_full.c:965364-965373.
     * `index = index + 1; if (index < count) Invoke("CreateBullet", delay);
     *  else in_atk = 0;`
     */
    struct BurstStep {
        /// The post-increment burst index (owner+0x80 after `index = index+1`).
        int index = 0;
        /// True -> Invoke("CreateBullet", delay) re-schedules the next shot
        /// (index < count). The Invoke itself + the SFX are OWNER side-effects.
        bool reschedule = false;
        /// in_atk after this pump (owner+0x84): stays set while rescheduling,
        /// cleared (kInAtkStopped) when the burst is exhausted (index >= count).
        bool inAtk = true;
    };

    /**
     * @brief Advance the burst counter one re-scheduled shot (c_count < 0 path).
     *
     * FAITHFUL: Gun019__CreateBullet @ game_full.c:965364-965373. The decomp
     * increments index (owner+0x80) FIRST, then compares to count (owner+0x78):
     * `index < count` keeps the burst alive (Invoke re-schedule) and leaves
     * in_atk set; otherwise in_atk (owner+0x84) is cleared to 0 to stop. The
     * PlayEffect at the top of the branch and the Invoke re-schedule are OWNER
     * concerns; we model only the counter/limit/flag transition. NO RGRandom
     * draw on this path.
     * @param index the current burst index BEFORE the increment (owner+0x80).
     * @param count the burst total (owner+0x78).
     * @return the post-increment index, whether to re-schedule, and in_atk.
     */
    static BurstStep BurstAdvance(int index, int count);

    /**
     * @brief The scatter half-angle (the magnitude fed to the scatter draw) for
     *        a main shot: spread = angle + angle*recoil = angle*(1 + recoil).
     *
     * FAITHFUL: Gun019__CreateBullet @ game_full.c:965383-965384.
     * `fVar4 = (float)VectorSignedToFloat(angle 0x30);
     *  fVar4 = fVar4 + fVar4 * *(float *)(component + 0x20);`
     * `angle` is the owner's base scatter field (owner+0x30, an int the decomp
     * widens to float via the ARM vcvt Ghidra surfaces as VectorSignedToFloat);
     * `recoil` is a bullet-component float (component+0x20) supplied by the
     * owner. NO RGRandom draw here.
     * @param angle  the base scatter angle (owner+0x30), in degrees.
     * @param recoil the bullet recoil/spread factor (component+0x20).
     * @return the scatter half-angle magnitude used as the +/- bound below.
     */
    static float SpreadHalfAngle(float angle, float recoil);

    /**
     * @brief One main shot's scatter angle: Range(-spread, +spread).
     *
     * FAITHFUL: Gun019__CreateBullet @ game_full.c:965389
     * (`RGRandom__Range(owner+0x60, -fVar4, fVar4, 0)`). Draws EXACTLY ONE float
     * from this weapon's stream (max-INCLUSIVE float Range), the symmetric
     * single-shot scatter. `spread` is the value from SpreadHalfAngle. The
     * subsequent muzzle Transform.get_position bullet spawn (owner+0x4c) is the
     * OWNER's concern (truncated tail in the decomp).
     * @return the bullet's deterministic scatter angle within [-spread, spread].
     */
    float ScatterAngle(float spread);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< owner+0x60; advanced once per main shot only.
};

} // namespace Game

#endif /* GAME_GUN019_HPP */
