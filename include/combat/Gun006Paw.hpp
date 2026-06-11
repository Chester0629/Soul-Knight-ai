#ifndef GAME_GUN006PAW_HPP
#define GAME_GUN006PAW_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class Gun006Paw
 * @brief Faithful companion-sword buff math for the "Gun006Paw" weapon
 *        (Soul Knight 1.7.10, an RGWeapon subclass paired with a Gun006 sword).
 *
 * Per-content port. Gun006Paw is the "paw" half of a companion pair: on use it
 * walks its controller chain to find the linked sword's owning RGController and,
 * when that chain resolves, buffs a stat by its own atk and mirrors a target's
 * stat onto the sword's damage; when the chain does NOT resolve it falls back to
 * a fixed sword damage of atk + 3.
 *
 * Almost the entire body is OWNER-gated and is intentionally NOT modeled here:
 *   - get_sword (Gun006Paw__get_sword @ game_full.c:317082): a truncated
 *     GetComponent<Gun006> tail -- it fetches the linked sword Component. OWNER.
 *   - the UnityEngine.Object op_Implicit null-checks on the sword and its
 *     controller field (sword + 0x50). OWNER (Unity intrinsic).
 *   - the controller-type vtable walk: the two comparisons against
 *     RGController_TypeInfo using the interface-depth byte (RGController_TypeInfo
 *     + 0xac) to index the controller's interface-offset table (controller +
 *     0x58) -- this is the IL2CPP `is RGController` reflection check that decides
 *     success vs. fallback. The TypeInfo globals carry no recoverable value, so
 *     the branch SELECTION is owner-gated (see fabrication_flags); this brain
 *     models both arms of the recoverable scalar math and lets the caller say
 *     which arm the type-walk took.
 *
 * The recoverable pure scalar/state math that this brain models:
 *   - StatAfterBuff (success path, 317035): *(stat) = *(stat) + atk(this+0xc) --
 *     the linked controller's role_attribute stat (controller.role_attribute
 *     [0x44] + 0x40) is increased by this weapon's atk. Pure: stat + atk.
 *   - SwordDmgFromTarget (success path, 317062): sword.dmg (sword + 0x20) is set
 *     to the target stat read through controller+0x44 then +0x40. Pure copy: the
 *     sword damage BECOMES the supplied source-stat value.
 *   - FallbackSwordDmg (fallback path / LAB_003efbd4, 317074): sword.dmg
 *     (sword + 0x20) = atk(this+0xc) + 3. Pure: atk + 3.
 *
 * Determinism: NO Gun006Paw body draws from rg_random (the buff is pure integer
 * stat arithmetic, not random scatter), so this unit makes ZERO RNG draws. The
 * RGRandom member is carried only for owner-side parity/lockstep and is never
 * advanced; Seeded() lets a caller confirm the seed without a draw.
 *
 * @see recreation Enemy/RGEController.cs (field-offset reference: role_attribute
 *      at controller 0x70 chain, stat objects reached via 0x44/0x40);
 *      FAITHFUL: Gun006Paw @ game_full.c:316984-317090.
 */
class Gun006Paw {
public:
    /// Fixed buff added to atk on the fallback path.
    /// FAITHFUL: Gun006Paw__StartUseWeapon @ 317074 -- sword[0x20] = atk(0xc) + 3.
    static constexpr int kFallbackDmgBonus = 3;

    Gun006Paw() = default;

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Success-path stat buff: the linked controller's stat += this atk.
     *
     * FAITHFUL: Gun006Paw__StartUseWeapon @ game_full.c:317035 --
     *   piVar3 = controller.role_attribute(+0x40); *piVar3 = *piVar3 + atk(0xc).
     * The +0x40 stat field is read/written as a raw int (4-byte access), matching
     * the decomp's `*(int *)` widths. Pure scalar add; no clamp exists in the
     * original. The atk source is this weapon's atk field (this+0xc), supplied by
     * the caller. NO RGRandom draw.
     *
     * @param controllerStat the current value of controller.role_attribute[+0x40].
     * @param atk            this weapon's atk (this+0xc).
     * @return the buffed stat value (controllerStat + atk).
     */
    static int StatAfterBuff(int controllerStat, int atk);

    /**
     * @brief Success-path sword damage: copy the target's source stat onto sword.
     *
     * FAITHFUL: Gun006Paw__StartUseWeapon @ game_full.c:317062 --
     *   iVar2 = *(controller + 0x44); *(sword + 0x20) = *(iVar2 + 0x40).
     * The sword's damage (sword + 0x20) BECOMES the value read from the
     * stat-source object reached via controller+0x44 then +0x40. This is a pure
     * pass-through: the sword damage equals the supplied source-stat value. The
     * pointer walk (controller+0x44 -> object, +0x40 -> int) is the owner's job;
     * the recoverable math is the assignment. NO RGRandom draw.
     *
     * @param targetSourceStat the int read from controller+0x44 then +0x40.
     * @return the new sword damage (== targetSourceStat).
     */
    static int SwordDmgFromTarget(int targetSourceStat);

    /**
     * @brief Fallback-path sword damage when the controller chain does not resolve.
     *
     * FAITHFUL: Gun006Paw__StartUseWeapon @ game_full.c:317074 (LAB_003efbd4) --
     *   *(int *)(sword + 0x20) = *(int *)(this + 0xc) + 3.
     * Pure scalar: atk + 3. NO RGRandom draw.
     *
     * @param atk this weapon's atk (this+0xc).
     * @return the fallback sword damage (atk + kFallbackDmgBonus).
     */
    static int FallbackSwordDmg(int atk);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{}; ///< deterministic stream; never advanced on any path.
};

} // namespace Game

#endif /* GAME_GUN006PAW_HPP */
