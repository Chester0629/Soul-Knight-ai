#ifndef LOOT_TABLE_HPP
#define LOOT_TABLE_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace Game {

class RGRandom;

/**
 * @class LootTable
 * @brief Faithful weighted chest-loot roll from droptables.json.
 *
 * Ports the loot roll Soul Knight 1.7.10 runs in @c RGChest.OpenChest
 * (@c RGChest$$OpenChest @ 0x5AD44C). Each chest carries a tier (chest_level
 * 0..6); the tier owns a weighted entry table { drop_id, rate } plus a cached
 * @c total == sum(rate). The roll is a cumulative-weight bucket select:
 *
 *     total = sum(rate)                       // precomputed per tier
 *     r     = RGRandom.Range(0, total)        // Unity int, max EXCLUSIVE
 *     for each entry: r -= entry.rate; if (r < 0) -> entry wins
 *     fallback: last entry                    // numerical fall-through guard
 *
 * The roll is driven by a caller-owned per-chest @c RGRandom stream, so the same
 * seed always yields the same drop - the property the original relies on to keep
 * host and clients in sync over the network (every peer replays the roll with
 * the authoritative seed).
 *
 * Data is read from @c <resourceRoot>/data/droptables.json via Util::DataStore:
 * a JSON object keyed "0".."6", each value an array of
 * @c {"weapon_id"|"path": string, "rate": int}. Loading is defensive - missing
 * tiers, malformed entries, or a missing file leave the affected tier empty
 * rather than throwing.
 */
class LootTable {
public:
    /// One weighted drop entry: a drop id (weapon/resource) and its weight.
    struct Entry {
        std::string dropId;     ///< e.g. "weapon_002"
        int rate = 0;           ///< selection weight (data is uniformly 2)
    };

    /// One chest tier: its entries plus the cached total weight (sum of rate),
    /// which is the @c Range(0, total) upper bound (exclusive) for the roll.
    struct Tier {
        int level = 0;                  ///< chest_level this tier serves
        std::vector<Entry> entries;     ///< weighted drop entries
        int total = 0;                  ///< precomputed sum of entry rates
    };

    /**
     * @brief Load every tier from @c <resourceRoot>/data/droptables.json.
     * @param resourceRoot The Resources root (e.g. the RESOURCE_DIR macro).
     * @return true if droptables.json parsed as a non-empty keyed object and at
     *         least one tier holds a usable (total > 0) entry table.
     *
     * Replaces any previously loaded tables. Entries with a non-positive rate
     * are kept (they simply never win) but contribute to @c total exactly as the
     * original sums @c ChestInfo.rates.
     */
    bool LoadAll(const std::string &resourceRoot);

    /**
     * @brief Roll a single weighted drop for @p tier.
     * @param tier The requested chest_level; clamped to the nearest available
     *             tier (highest tier whose level <= requested, else the lowest).
     * @param rng  The per-chest deterministic stream; exactly one int draw is
     *             consumed per roll (matching @c RGChest.OpenChest).
     * @return The winning drop id, or "" if no tier is usable (empty/total<=0).
     *
     * FAITHFUL to RGChest$$OpenChest @ 0x5AD44C: r = Range(0, total) then walk
     * entries subtracting each rate; first entry with r < 0 wins; on numerical
     * fall-through the last entry is returned.
     */
    std::string Roll(int tier, RGRandom &rng) const;

    /**
     * @brief Resolve @p level to a loaded tier (same clamp rule as @ref Roll).
     * @return Pointer to the chosen tier, or nullptr if no tiers are loaded.
     */
    const Tier *GetTier(int level) const;

    /// All loaded tiers, in ascending @c level order.
    const std::vector<Tier> &Tiers() const { return m_Tiers; }

    /// Number of loaded tiers.
    std::size_t TierCount() const { return m_Tiers.size(); }

private:
    std::vector<Tier> m_Tiers; ///< ascending by level; totals precomputed
};

} // namespace Game

#endif /* LOOT_TABLE_HPP */
