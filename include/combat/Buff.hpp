#ifndef GAME_COMBAT_BUFF_HPP
#define GAME_COMBAT_BUFF_HPP

#include <vector>

namespace Game {
namespace Combat {

/**
 * @brief The six "core" status-effect buffs shipped in Resources/data/buffs.json
 *        (BuffArmor, BuffChangeSpeed, BuffElectric, BuffFire, BuffIce, BuffPoison).
 *
 * FAITHFUL: decompile evidence for each .ctor/Start/GetHurt/BuffEnd lives at
 * D:\Projects\Soul Knight\_reverse\ghidra_export\game_typed.c:86460-87048 (mirrored,
 * truncated, in game_full.c around 961063-961320). Every ctor is confirmed to set
 * `is_enemy` + `buff_time`; BuffChangeSpeed__Start is confirmed to call
 * RoleAttribute__ChangeSpeed(role, tag, value, time, changeValue) (game_typed.c
 * 86553-86555); only the CALL is confirmed -- that the delta is ADDITIVE is
 * INFERRED (T-32) from the parameter's name/signature, see below.
 * BuffIce__BuffEnd is confirmed to call CancelInvoke (game_typed.c 86943-86945 /
 * game_full.c:961235); that only proves SOME previously scheduled callback is
 * cancelled at end -- it does NOT prove the buff's END itself is scheduled (vs.
 * immediate), so do not over-claim a scheduled-end mechanism from this alone.
 * BuffFire__GetHurt (game_typed.c 86867-86886) / BuffPoison equivalents are present
 * but their bodies are obfuscation-guard tail calls with NO visible numeric literal
 * (tick interval, tick damage, stun length): those numbers are INFERRED (T-32), see
 * buffs.json's "_inferred" block and tools/build_gamedata.py's build_buff_table().
 */
enum class BuffKind {
    Fire,
    Poison,
    Ice,
    Electric,
    Armor,
    ChangeSpeed,
};

/// Stable short name for logging ("Buff: Fire applied to ..."). Never null.
const char *BuffKindName(BuffKind kind);

/**
 * @struct BuffParams
 * @brief Fully-resolved numeric parameters for one buff application, built from a
 *        BuffDef (real fields) + its "_inferred" JSON block (numbers the decompile
 *        does not show). See Game::BuffParamsFrom (data/GameData.hpp/.cpp).
 */
struct BuffParams {
    BuffKind kind = BuffKind::Fire;
    float durationSec = 0.0F;      ///< FAITHFUL: BuffDef::buffTime (buff_time field).
    float tickSec = 0.0F;          ///< INFERRED (T-32): Fire/Poison damage-over-time cadence.
    int tickDamage = 0;            ///< INFERRED (T-32): Fire/Poison damage per tick.
    /// Multiplicative movement-speed factor applied while the buff is active
    /// (1.0 = no change). Ice/Electric use 0.0 (a full freeze) -- INFERRED (T-32)
    /// stun-via-zero-speed modeling; BuffIce__BuffEnd's CancelInvoke (game_typed.c
    /// 86943-86945) only shows a scheduled callback being cancelled, it does not
    /// prove a freeze nor a scheduled end.
    /// ChangeSpeed: the call RoleAttribute__ChangeSpeed(role, tag, value, time,
    /// changeValue) is decompile-confirmed (game_typed.c 86553-86555); that it is
    /// ADDITIVE is INFERRED from the changeValue parameter's name/signature, not
    /// from a recovered body. Implementing it as a multiplier at all is likewise
    /// INFERRED (T-32): the sim's movement pipeline has no baseline "1.0 unit"
    /// speed to add onto uniformly, so build_buff_table() converts the additive
    /// delta with speed_mul = max(0, 1 + changeValue) -- clamped to a minimum of 0
    /// (no upper clamp) -- at data-build time, documented per-row in
    /// "_inferred._why".
    float speedMul = 1.0F;
    /// INFERRED (T-32): BuffArmor__GetArmor is confirmed to exist (game_typed.c
    /// 86492-86503, gated on !is_enemy), but its body is a truncated tail call --
    /// the "temporary armor granted while active, reverted when the buff ends"
    /// semantic and the +1 magnitude are both inferred, NOT proven by the mere
    /// existence of that function.
    int armorDelta = 0;
    bool refreshOnReapply = true;  ///< INFERRED (T-32): reapplying refreshes, does not stack.
};

/// One buff instance currently ticking on an entity.
struct ActiveBuff {
    BuffParams p{};
    float remainingSec = 0.0F;
    float tickAccum = 0.0F;
    /// item 5 (R18/Codex block, kill-attribution): true iff this buff instance was
    /// applied as the result of a PLAYER-sourced hit (a player bullet's on-hit buff
    /// trigger). Sim-side bookkeeping, NOT decompile state -- carried so a later
    /// DoT tick from this buff can correctly attribute EntityState::deathSource
    /// ("last damage source was the player", not "was ever hit by the player").
    bool fromPlayer = false;
};

/**
 * @class BuffList
 * @brief Engine-free per-entity buff bag: apply, advance by a fixed dt, and query.
 *
 * Reapplying the same BuffKind refreshes remainingSec back to durationSec (does NOT
 * stack a second instance) -- INFERRED (T-32), matching Soul Knight's usual "buff
 * icon resets, doesn't double" status-effect convention (common knowledge, not
 * decompile-visible for this title's tick cadence).
 */
class BuffList {
public:
    /// One Tick()'s resolved output: total damage to apply this step, the combined
    /// movement-speed multiplier (product of all active buffs' speedMul), whether
    /// the entity is frozen (speedMul reached exactly 0), the net armor delta from
    /// still-active Armor buffs, and which kinds expired THIS step (for the
    /// "Buff: <kind> ended" log line).
    struct TickResult {
        /// Total DoT damage this tick, from EVERY source combined (== damageFromPlayer
        /// + damageFromOther). Kept for callers that don't care about attribution
        /// (e.g. BuffTest.cpp, the player-DoT path in Simulation::TickBuffs).
        int damage = 0;
        float speedMul = 1.0F;
        bool frozen = false;
        int armorDelta = 0;
        std::vector<BuffKind> ended;
        /// item 2 (R19 kill-attribution fix): `damage` broken out PER SOURCE, so a
        /// caller that needs to know exactly which tick killed an entity
        /// (Simulation::TickBuffs) can apply the two portions as SEPARATE damage
        /// events instead of one merged one. Pre-fix, a single player fire DoT and a
        /// single non-player poison DoT ticking the SAME step merged into one
        /// HitResult with a bool "was any contributing source the player" flag --
        /// so an enemy killed by the non-player tick could still fire
        /// ICharSkill::OnEnemyKilled() just because a player DoT happened to also be
        /// active that step. Documented attribution rule (Simulation::TickBuffs):
        /// apply damageFromOther FIRST, then damageFromPlayer, skipping the 2nd
        /// portion once the entity is already dead from the 1st -- so
        /// EntityState::deathSource reflects whichever portion actually crossed
        /// hp<=0, not merely "was a player-sourced buff active this tick".
        int damageFromPlayer = 0;
        int damageFromOther = 0;
    };

    /// Apply (or refresh) a buff. Reapplying the same kind resets remainingSec to
    /// durationSec rather than stacking a second instance. @p fromPlayer (item 5)
    /// records whether this application was player-sourced (default false, so every
    /// existing call site -- including the manual/test ApplyBuff hook -- keeps its
    /// prior behaviour unless it opts in).
    void Apply(const BuffParams &params, bool fromPlayer = false);

    /// Advance every active buff by dtSec (the sim's fixed step). Fire/Poison
    /// accumulate tickAccum and emit tickDamage once per full tickSec elapsed.
    /// Only the slice of dtSec that falls inside the buff's remaining lifetime is
    /// credited, so the total tick count is exactly floor(durationSec / tickSec)
    /// even when durationSec is not a multiple of the fixed step (a boundary tick
    /// that lands exactly on expiry still fires; one past expiry never does).
    TickResult Tick(float dtSec);

    /// True iff a buff of this kind is currently active.
    bool Has(BuffKind kind) const;

    /// Remove every active buff (no ended-events emitted; used for entity death/reset).
    void Clear();

private:
    std::vector<ActiveBuff> m_Active;
};

} // namespace Combat
} // namespace Game

#endif /* GAME_COMBAT_BUFF_HPP */
