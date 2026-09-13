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
 * ~86553-86555); BuffIce__BuffEnd is confirmed to call CancelInvoke (game_typed.c
 * ~86936-86948 / game_full.c:961235), i.e. Ice ends via a scheduled callback, not an
 * immediate transition -- the only visible evidence for a TIMED end among the six.
 * BuffFire__GetHurt / BuffPoison equivalents are present but their bodies are
 * obfuscation-guard tail calls with NO visible numeric literal (tick interval, tick
 * damage, stun length): those numbers are INFERRED (T-32), see buffs.json's
 * "_inferred" block and tools/build_gamedata.py's build_buff_table().
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
    /// stun-via-zero-speed modeling of the decompile-confirmed timed BuffIce end.
    /// ChangeSpeed's real mechanic is RoleAttribute__ChangeSpeed, an ADDITIVE speed
    /// delta (decompile-confirmed call, game_typed.c ~86553-86555) using the data
    /// `changeValue` (e.g. -0.75); the sim's movement pipeline has no baseline
    /// "1.0 unit" speed to add onto uniformly, so build_buff_table() converts that
    /// additive delta into an equivalent multiplier (clamped to [0,1]) at data-build
    /// time -- INFERRED (T-32) conversion, documented per-row in "_inferred._why".
    float speedMul = 1.0F;
    int armorDelta = 0;            ///< FAITHFUL: BuffArmor__GetArmor exists (armor while active).
    bool refreshOnReapply = true;  ///< INFERRED (T-32): reapplying refreshes, does not stack.
};

/// One buff instance currently ticking on an entity.
struct ActiveBuff {
    BuffParams p{};
    float remainingSec = 0.0F;
    float tickAccum = 0.0F;
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
        int damage = 0;
        float speedMul = 1.0F;
        bool frozen = false;
        int armorDelta = 0;
        std::vector<BuffKind> ended;
    };

    /// Apply (or refresh) a buff. Reapplying the same kind resets remainingSec to
    /// durationSec rather than stacking a second instance.
    void Apply(const BuffParams &params);

    /// Advance every active buff by dtSec (the sim's fixed step). Fire/Poison
    /// accumulate tickAccum and emit tickDamage once per full tickSec elapsed
    /// (an exhausted buff still fires its last tick if the boundary lands exactly
    /// on expiry -- matches "duration counts down, ends exactly at buff_time").
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
