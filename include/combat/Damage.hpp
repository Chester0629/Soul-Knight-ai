#ifndef GAME_COMBAT_DAMAGE_HPP
#define GAME_COMBAT_DAMAGE_HPP

#include "combat/CombatStats.hpp"
#include "data/RGRandom.hpp"

namespace Game {
namespace Combat {

/**
 * @brief Hard caps on the repel (knockback) impulse magnitude.
 *
 * FAITHFUL: RGBaseController__GetForce @ game_full.c:467560 clamps to 30.0f for
 * the player/base controller; RGEController__GetForce @ game_full.c:473583 clamps
 * to 28.0f for enemies. The bullet trigger also clamps to 30 before handing the
 * impulse off (MaxRepel), so the player path sees a single 30 cap while the enemy
 * path is tightened further to 28 inside GetForce.
 */
constexpr float kPlayerRepelCap = 30.0F; ///< RGBaseController__GetForce cap.
constexpr float kEnemyRepelCap = 28.0F;  ///< RGEController__GetForce cap.

/**
 * @brief Co-op + ice-buff damage factor.
 *
 * FAITHFUL: RGBulletTrigger__GetDamageFactor @ game_full.c:469729 returns 0.5f
 * (0x3f000000) when the bullet carries the ice buff AND the RGGameProcess mode
 * flag (reduced ice damage in challenge/co-op) is set; otherwise 1.0f.
 */
constexpr float kIceCoopDamageFactor = 0.5F;
constexpr float kNoDamageFactor = 1.0F;

/**
 * @brief Default crit damage multiplier (critic_factor, bullet field @ 0x3C).
 *
 * RECOVERED (report #4): @c critic_factor is a runtime bullet-trigger field, not a
 * data-table value (no shipped JSON carries it). Its default is decomp-confirmed
 * as 2.0f: RGBulletTrigger__RemoveEffectTrigger writes 0x40000000 (== 2.0f) to
 * +0x3C (game_full.c:469815), and RGBulletTrigger__AddEffectTrigger lowers it to
 * 0x3f800000 (== 1.0f) while a BuffEffectTrigger is attached (game_full.c:469656).
 * So crit doubles damage by default and is suppressed to 1.0f under that buff. The
 * 1.0f suppression case is representable via the per-call @ref AttackerInput
 * override; the 2.0f default below is the recovered value, not a placeholder.
 */
constexpr float kDefaultCritFactor = 2.0F;

/// Crit roll bound: Random.Range(0, 100). Crit when roll < critical.
constexpr int kCritRollMax = 100;

/**
 * @struct AttackerInput
 * @brief Everything the attacker (bullet) contributes to one hit, before the
 *        defender side resolves armor/hp.
 *
 * Mirrors the RGBulletTrigger fields consulted in OnTriggerEnter2D:
 *   damage (0x1C), repel (0x10), critical (0x28, 0..100), critic_factor (0x3C),
 *   has_ice_buff (0x40). @ref repelInputMagnitude is the pre-clamp impulse the
 *   bullet computed (repel * velocity for the player path, repel * dealtDamage
 *   for the enemy path) -- supplied by the caller since it depends on bullet
 *   velocity which lives outside pure damage logic.
 */
struct AttackerInput {
    int baseDamage = 0;            ///< bullet.damage (RGBulletTrigger 0x1C).
    int critical = 0;              ///< crit chance 0..100 (RGBulletTrigger 0x28).
    float critFactor = kDefaultCritFactor; ///< critic_factor (0x3C); see flag.
    float repelInputMagnitude = 0.0F;      ///< pre-clamp repel * speed (or * dmg).
    bool iceCoopHalving = false;   ///< has_ice_buff && co-op reduced-ice mode set.
};

/**
 * @struct HitResult
 * @brief The resolved attacker-side numbers handed to the defender's GetHurt.
 */
struct HitResult {
    int finalDamage = 0;        ///< damage after factor + crit, fed to GetHurt.
    bool isCrit = false;        ///< true iff the crit roll landed (roll < critical).
    float repelMagnitude = 0.0F;///< clamped knockback magnitude applied to target.
};

/**
 * @brief Whether the defender is the player (30 cap) or an enemy (28 cap).
 */
enum class Defender { PLAYER, ENEMY };

/**
 * @brief Resolve the attacker side of one hit: damage factor, crit roll, crit
 *        scaling, and the repel impulse (doubled on crit, then HARD-capped).
 *
 * FAITHFUL: RGBulletTrigger.OnTriggerEnter2D (FUN_005b5c14, rva 0x5A5C14) crit
 * branch, GetDamageFactor (rva 0x5A9BBC), and the GetForce caps. Recovered chain:
 *   base   = baseDamage;
 *   base  *= iceCoop ? 0.5 : 1.0;       // GetDamageFactor (enemy path)
 *   roll   = RGRandom.Range(0, 100);     // crit roll
 *   final  = (roll < critical) ? (int)(base * critFactor) : base;
 *   repel  = doubled-first-on-crit, then min(repel, cap).
 *
 * The crit roll ALWAYS consumes one RGRandom draw (faithful to the original,
 * which rolls regardless of crit chance), keeping the deterministic stream in
 * lockstep with the binary.
 *
 * @param in   Attacker contribution (damage / crit / repel / factor).
 * @param rng  Per-instance deterministic stream (the crit roll source).
 * @param who  Defender side, selecting the 30 (player) vs 28 (enemy) repel cap.
 */
HitResult ResolveHit(const AttackerInput &in, RGRandom &rng, Defender who);

/**
 * @brief Apply a resolved hit to a PLAYER defender (armor-then-hp).
 *
 * FAITHFUL: RGController__GetHurt -> HurtArmor -> HurtHp. The i-frame / alive
 * gate (awake && can_hurt) is the caller's responsibility (passed via @p canHurt);
 * when ungated the hit is ignored entirely and nothing mutates -- exactly like the
 * decompiled early-return.
 *
 * @param stats   The player's vitals (mutated in place).
 * @param result  Output of @ref ResolveHit.
 * @param canHurt awake && can_hurt gate; false -> no-op (i-frames / not alive).
 * @return true iff the player is dead (hp <= 0) after applying.
 */
bool ApplyToPlayer(CombatStats &stats, const HitResult &result, bool canHurt);

/**
 * @brief Apply a resolved hit to an ENEMY defender (straight hp, NO armor).
 *
 * FAITHFUL: RGEController__GetHurt -> SyncGetHurt. The awake && !dead gate is the
 * caller's responsibility (passed via @p canHurt); false -> no-op.
 *
 * @param stats   The enemy's vitals (mutated in place).
 * @param result  Output of @ref ResolveHit.
 * @param canHurt awake && !dead gate; false -> no-op (not active / already dying).
 * @return true iff the enemy is dead (hp <= 0) after applying.
 */
bool ApplyToEnemy(CombatStats &stats, const HitResult &result, bool canHurt);

} // namespace Combat
} // namespace Game

#endif /* GAME_COMBAT_DAMAGE_HPP */
