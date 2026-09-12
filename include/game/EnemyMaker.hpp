#ifndef GAME_ENEMY_MAKER_HPP
#define GAME_ENEMY_MAKER_HPP

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @brief The original EnemyMaker's spawn budget -- map work M1.
 *
 * FAITHFUL: EnemyMaker__StartEnemyMaker @ game_full.c:682128-682234 (the
 * visible part): the maker's `level` selects an `enemy_point` budget
 * `rg_random.Range(lo, hi)`:
 *   level 1 -> [8,15)   level 2 -> [15,24)  level 3 -> [24,36)
 *   level 4 -> [36,44)  level 5 -> [44,52)  level >= 6 -> 60 (no draw)
 *   level -1 -> 9999 (endless)  level 0 -> nothing spawns
 * CreatePart then instantiates enemies until the budget is spent, each pick
 * charging its RGEController.consume. (The pool pick itself is
 * `enemy_list[Range(0, count)]` on MapManager's weighted list -- duplicates in
 * enemy_list ARE the weights.)
 *
 * NOT visible (T-26 Ghidra truncation after the singleton call): the body of
 * the big-room level bump. See EnemyMakerLevel below -- INFERRED (T-05).
 */
inline std::pair<int, int> EnemyPointRange(int level) {
    switch (level) {
    case 1: return {8, 15};
    case 2: return {15, 24};
    case 3: return {24, 36};
    case 4: return {36, 44};
    case 5: return {44, 52};
    case 0: return {0, 0};
    case -1: return {9999, 9999};
    default: return level >= 6 ? std::pair<int, int>{60, 60} : std::pair<int, int>{0, 0};
    }
}

/// INFERRED (T-05): StartEnemyMaker @ game_full.c:682128-682152 gates on a
/// room flag (RGRoomX+0x28 != 0) and level > 0, then tests room_width (+0x14)
/// > 15 and > 21 and room_height (+0x18) > 15 and > 21 in nested blocks whose
/// bodies were truncated; the port reads each passed test as +1 level. The flag
/// and the exact increments are not recovered.
inline int EnemyMakerLevel(int stage, int roomWidth, int roomHeight) {
    if (stage <= 0) {
        return stage;
    }
    int level = stage;
    for (int dim : {roomWidth, roomHeight}) {
        if (dim > 15) {
            ++level;
        }
        if (dim > 21) {
            ++level;
        }
    }
    return level;
}

/// Draw the room's enemy_point budget (ONE Range draw when lo != hi).
inline int RollEnemyPoints(int level, RGRandom &rng) {
    const auto [lo, hi] = EnemyPointRange(level);
    if (lo >= hi) {
        return lo;
    }
    return rng.Range(lo, hi);
}

/**
 * @brief Spend @p points on picks from the weighted @p pool (MapManager.enemy_list
 *        order), charging each pick's consume (>= 1). Stops at @p cap picks so a
 *        tiny room can never be flooded (PORT CHOICE T-04: cap 24 + consume >= 1;
 *        the original relies on its spawn-point list running out).
 */
inline std::vector<std::string>
PlanRoomSpawns(const std::vector<std::string> &pool,
               const std::function<int(const std::string &)> &consumeOf, int points,
               RGRandom &rng, int cap = 24) {
    std::vector<std::string> out;
    if (pool.empty()) {
        return out;
    }
    while (points > 0 && static_cast<int>(out.size()) < cap) {
        const std::string &id = pool[static_cast<std::size_t>(
            rng.Range(0, static_cast<int>(pool.size())))];
        points -= std::max(1, consumeOf(id));
        out.push_back(id);
    }
    return out;
}

} // namespace Game

#endif /* GAME_ENEMY_MAKER_HPP */
