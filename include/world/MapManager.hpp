#ifndef GAME_MAP_MANAGER_HPP
#define GAME_MAP_MANAGER_HPP

#include <array>
#include <vector>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @struct RoomCell
 * @brief One placed room on the dungeon grid.
 *
 * @c type is the room_relation code: 1 = normal/start, 2 = special/reward,
 * 3 = badass/elite. @c entrance holds the door flags per direction in the same
 * order RoomGen consumes them -- [0]=EAST(+x), [1]=NORTH(+y), [2]=WEST(-x),
 * [3]=SOUTH(-y) -- with 1 meaning an orthogonally-adjacent room exists that way.
 */
struct RoomCell {
    int gridX = 0;
    int gridY = 0;
    int type = 1;
    std::array<int, 4> entrance{0, 0, 0, 0};
};

/**
 * @class MapManager
 * @brief Faithful, deterministic dungeon-floor layout (the RGRoomX-less core of
 *        the original @c MapManager.CreateMap random walk).
 *
 * Lays rooms on a square grid by a seeded random walk: starting from the centre
 * (the spawn room), each step branches from a random already-placed room in a
 * random orthogonal direction onto a free cell, until @c mapLong rooms exist.
 * One special (type 2) and one badass (type 3) room may appear, gated by
 * @c ranRoomProbability. Each placed room's @ref RoomCell::entrance flags are
 * then derived from its placed neighbours, ready to feed @c RoomGen.
 *
 * Determinism: every roll goes through one seeded @ref RGRandom stream in the
 * exact order of the original (Range(0,count) to pick a source room, Range(0,4)
 * for the direction, Range(0,100) for the special/badass gates), so the same
 * seed reproduces the identical floor. Engine-free and unit-testable.
 *
 * @see MapManager.cs (recreation). The original method bodies were not
 *      byte-recoverable (IL2CPP thunks); this follows the documented grid
 *      algorithm and is flagged accordingly in the recreation notes.
 */
class MapManager {
public:
    struct Options {
        int mapLong = 6;             ///< Target room count for this floor.
        int ranRoomProbability = 25; ///< % chance per step of a special/badass room.
        int gridSize = 0;            ///< 0 => auto: max(8, mapLong*2+1).
    };

    MapManager(int seed, const Options &options);

    /// All placed rooms, in placement order (index 0 is the start room).
    const std::vector<RoomCell> &Rooms() const { return m_Rooms; }

    /// Index into @ref Rooms of the start (spawn) room (always 0 here).
    int StartIndex() const { return 0; }

    /// The square grid side length.
    int GridSize() const { return m_GridSize; }

    /// room_relation code at grid (gx,gy): 0 = no room, else the room type.
    int RoomType(int gx, int gy) const;

    /// @return true if a room occupies grid cell (gx,gy) (faithful IsInMapList).
    bool HasRoom(int gx, int gy) const;

    /// @return true if every placed room is reachable from the start (BFS over
    ///         orthogonal room adjacency). True by construction (the walk is a
    ///         tree), verified defensively.
    bool Connected() const;

private:
    int Index(int gx, int gy) const { return gx * m_GridSize + gy; }

    RGRandom m_Rng;
    int m_GridSize = 0;
    std::vector<int> m_Relation;   ///< room_relation grid (flat, gx*size+gy).
    std::vector<RoomCell> m_Rooms; ///< map_list, with derived entrance flags.
};

} // namespace Game

#endif /* GAME_MAP_MANAGER_HPP */
