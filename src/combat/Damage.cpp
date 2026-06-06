#include "combat/Damage.hpp"

namespace Game {
namespace Combat {

// FAITHFUL: RGBulletTrigger.OnTriggerEnter2D crit branch @ FUN_005b5c14 (rva
// 0x5A5C14) + GetDamageFactor @ rva 0x5A9BBC + GetForce caps (player 0x59FE64 /
// rva-decode 467560 = 30f, enemy 473583 = 28f).
HitResult ResolveHit(const AttackerInput &in, RGRandom &rng, Defender who) {
    HitResult out;

    // base *= GetDamageFactor(): 0.5 under ice-buff + co-op reduced-ice mode,
    // else 1.0. The factor scales the integer base via float (truncating cast),
    // matching damage = (int)((float)damage * GetDamageFactor()).
    const float factor = in.iceCoopHalving ? kIceCoopDamageFactor : kNoDamageFactor;
    const int factored = static_cast<int>(static_cast<float>(in.baseDamage) * factor);

    // Crit roll: roll = Random.Range(0, 100); crit when roll < critical. The roll
    // is ALWAYS drawn (even at critical == 0) so the RNG stream stays in lockstep.
    const int roll = rng.Range(0, kCritRollMax);
    out.isCrit = roll < in.critical;

    // final = crit ? (int)(base * critic_factor) : base.
    out.finalDamage =
        out.isCrit
            ? static_cast<int>(static_cast<float>(factored) * in.critFactor)
            : factored;

    // Repel: doubled FIRST on a crit, then HARD-capped (player 30 / enemy 28).
    float repel = in.repelInputMagnitude;
    if (out.isCrit) {
        repel = repel + repel; // doubled before the clamp (decomp order)
    }
    const float cap = (who == Defender::PLAYER) ? kPlayerRepelCap : kEnemyRepelCap;
    if (repel > cap) {
        repel = cap;
    }
    out.repelMagnitude = repel;

    return out;
}

// FAITHFUL: RGController__GetHurt -> HurtArmor -> HurtHp (player armor->hp split).
bool ApplyToPlayer(CombatStats &stats, const HitResult &result, bool canHurt) {
    if (!canHurt) {
        return stats.IsDead(); // i-frame / not-alive gate: ignore the hit entirely.
    }
    return stats.ApplyPlayerDamage(result.finalDamage);
}

// FAITHFUL: RGEController__GetHurt -> SyncGetHurt (straight hp, NO armor).
bool ApplyToEnemy(CombatStats &stats, const HitResult &result, bool canHurt) {
    if (!canHurt) {
        return stats.IsDead(); // awake && !dead gate: ignore the hit entirely.
    }
    return stats.ApplyEnemyDamage(result.finalDamage);
}

} // namespace Combat
} // namespace Game
