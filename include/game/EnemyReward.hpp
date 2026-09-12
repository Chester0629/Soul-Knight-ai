#ifndef GAME_ENEMY_REWARD_HPP
#define GAME_ENEMY_REWARD_HPP

#include <vector>

#include "data/GameData.hpp"
#include "data/RGRandom.hpp"

namespace Game {

/**
 * @brief What a killed hostile drops -- B3 economy, the port of
 *        RGEController.GetReward.
 *
 * FAITHFUL data (res/register prefabs, RGCoin / RGEnergy components):
 *   - coin_0 "gold"   value 5, coin_1 "silver" value 3, coin_2 "copper" value 1
 *     (PrefabManager.GetCoin(index) clamps index to 0..2 -- exactly three coins);
 *   - energy (PrefabName.energy = 8) value 8.
 *   All four share speed 20 (energy 14) / magnet range 6 world units.
 *
 * RGEController.GetReward (game_full.c, body truncated by Ghidra after the gate):
 *   `RGRandom.Range(0, 100) < reward_rate` gates the drop; then the enemy's
 *   `reward_value[4]` (enemies.csv, Unity int[4]) gives the COUNT of each kind:
 *   [gold, silver, copper, energy] -- the three coin slots line up with GetCoin's
 *   0..2 clamp and the 4th with the energy prefab. Evidence: slime [0,0,1,1] = one
 *   copper + one energy; bosses [3,3,0,10] = 3 gold + 3 silver + 10 energy.
 *   INFERRED layout (T-18: the instantiate loop itself did not survive decompilation).
 */
enum class RewardKind { Gold = 0, Silver = 1, Copper = 2, Energy = 3 };

/// P1.12: the per-kind drop values (RGCoin.value / RGEnergy.value) come from
/// pickups.json (res/register prefabs), never from constants.
struct RewardValues {
    int gold = 0;
    int silver = 0;
    int copper = 0;
    int energy = 0;
    int Of(RewardKind k) const {
        switch (k) {
        case RewardKind::Gold: return gold;
        case RewardKind::Silver: return silver;
        case RewardKind::Copper: return copper;
        case RewardKind::Energy: return energy;
        }
        return 0;
    }
};

inline const char *RewardKindName(RewardKind k) {
    switch (k) {
    case RewardKind::Gold: return "gold";
    case RewardKind::Silver: return "silver";
    case RewardKind::Copper: return "copper";
    case RewardKind::Energy: return "energy";
    }
    return "";
}

/// Build the value table from GameData's pickups (0 for a missing kind).
inline RewardValues RewardValuesFrom(const GameData &gd) {
    RewardValues v;
    if (const PickupDef *p = gd.FindPickup("gold")) v.gold = p->value;
    if (const PickupDef *p = gd.FindPickup("silver")) v.silver = p->value;
    if (const PickupDef *p = gd.FindPickup("copper")) v.copper = p->value;
    if (const PickupDef *p = gd.FindPickup("energy")) v.energy = p->value;
    return v;
}

struct RewardDrop {
    RewardKind kind = RewardKind::Copper;
    int value = 0;
};

/// Roll one hostile's death reward. Draws EXACTLY one Range(0,100) from @p rng
/// (the reward_rate gate, RGEController.GetReward); a failed gate drops nothing.
/// Bosses carry reward_rate 100 so they always pay out.
inline std::vector<RewardDrop> RollEnemyReward(const EnemyDef &def, RGRandom &rng,
                                               const RewardValues &values) {
    std::vector<RewardDrop> out;
    // INFERRED (T-18): RGEController__GetReward shows RGRandom.Range(0,100) then is
    // truncated by the singleton call; the `< reward_rate` compare is inferred.
    if (rng.Range(0, 100) >= def.rewardRate) {
        return out;
    }
    for (int slot = 0; slot < 4; ++slot) {
        const auto kind = static_cast<RewardKind>(slot);
        for (int n = 0; n < def.rewardValue[slot]; ++n) {
            out.push_back(RewardDrop{kind, values.Of(kind)});
        }
    }
    return out;
}

} // namespace Game

#endif /* GAME_ENEMY_REWARD_HPP */
