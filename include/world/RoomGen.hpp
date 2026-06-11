#ifndef GAME_ROOMGEN_HPP
#define GAME_ROOMGEN_HPP

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RoomGen
 * @brief Pure, deterministic dungeon-room grid generator - the @c SetUpRoom
 *        build pipeline of Soul Knight 1.7.10's @c RGRoomX.
 *
 * Faithful port of @c RGRoomX's procedural (random_room) build path:
 *   SetRGRandomSeed -> SetUpRoom -> { CreateAisle, CreateFloor, CreateWall,
 *   CreateObstacle }. The output is the deterministic integer build grid
 *   (@c map[x,y]) plus the walkable floor-cell list (@c floor_list), exactly as
 *   the original computes them. Sprite / collider / prefab instantiation is the
 *   orchestrator's job and is intentionally NOT done here.
 *
 * The room lives on a fixed @c MAP_SIZE x @c MAP_SIZE logical grid (the decomp
 * constant @c 0x29 = 41). The room itself occupies @c Width() x @c Height()
 * cells; the build grid is sized exactly @c Width() x @c Height() (matching the
 * original's @c map = new int[room_width, room_height]) and is centred inside
 * the 41x41 space via @c room_x_offset / room_y_offset = (41 - size) / 2.
 *
 * Grid indexing is faithful to the IL2CPP @c int[room_width, room_height]
 * row-major layout observed in the decompilation: @c map[x,y] lives at linear
 * index @c x * room_height + y (x is the outer / row dimension).
 *
 * @par Cell-code legend (from RGRoomX__CreateObstacle / CreateAisle / SetUpRoom)
 *  - @c -2 : aisle / corridor path (carved by CreateAisle)
 *  - @c -1 : room border / big-obstacle interior
 *  - @c  0 : open floor (these become the @ref FloorList)
 *  - @c  1 : big / solid obstacle
 *  - @c  2 : small / destructible obstacle
 *  - @c  8 : 1x1 big-obstacle centre marker
 *  - @c  9 : 2x2 big-obstacle centre marker
 *  - @c 11 : door cell (corridor end)
 *  - @c >2 : indexed decorative obstacle variant
 *
 * All randomness flows through one seeded @ref RGRandom stream, so the same seed
 * (and same options/entrances) yields a bit-identical grid - the determinism the
 * original relies on for networked replay.
 */
class RoomGen {
public:
    /// The whole dungeon is logically MAP_SIZE x MAP_SIZE cells (decomp 0x29=41).
    static constexpr int MAP_SIZE = 41;

    /// Banded room-size table {15, 21, 25}; rooms pick width/height from this.
    static constexpr std::array<int, 3> kRoomSizes = {15, 21, 25};

    /**
     * @struct Options
     * @brief Inputs to the build that the original derives from level/game state.
     *
     * When @ref randomRoom is true, the room rolls its own width/height and
     * difficulty levels from the seeded stream (faithful to SetRGRandomSeed's
     * random_room branch). When false, the caller-supplied @ref roomWidth /
     * @ref roomHeight / @ref wallLevel / @ref obstacleLevel are used verbatim
     * (useful for tests and fixed-size rooms).
     */
    struct Options {
        bool randomRoom = true;   ///< Roll size + difficulty from the RNG.
        int floorIndex = 0;       ///< Current floor; floorIndex%5==1 narrows size roll.
        bool badassMode = false;  ///< Badass bumps special_box_rate to 20.

        // Used only when randomRoom == false (explicit room).
        int roomWidth = 15;       ///< Explicit room width (one of kRoomSizes).
        int roomHeight = 15;      ///< Explicit room height.
        int wallLevel = 1;        ///< Explicit big-obstacle level in [1,3].
        int obstacleLevel = 1;    ///< Explicit small-obstacle level in [1,3].
    };

    /**
     * @brief Build a room grid deterministically.
     *
     * @param seed      Deterministic seed (network-authoritative in the original).
     * @param entrance  Door flags per direction; index order matches the original
     *                  @c entrance[] consumed by CreateAisle:
     *                  [0]=EAST, [1]=NORTH, [2]=WEST, [3]=SOUTH. A value of 1
     *                  carves a corridor + door cells on that side.
     * @param options   Size / difficulty / badass inputs (see @ref Options).
     *
     * Faithful to RGRoomX__SetRGRandomSeed + SetUpRoom: seeds the stream, (for
     * random rooms) rolls size and difficulty, centres the room, stamps the
     * border, then runs CreateAisle / CreateFloor / CreateWall / CreateObstacle
     * in that exact order so the RNG sequence matches the original.
     */
    RoomGen(int seed, const std::array<int, 4> &entrance, const Options &options);

    /// @return Room width in cells (the x span of the build grid).
    int Width() const { return m_RoomWidth; }
    /// @return Room height in cells (the y span of the build grid).
    int Height() const { return m_RoomHeight; }
    /// @return room_x_offset = (MAP_SIZE - width)/2 (centring offset, x).
    int OffsetX() const { return m_RoomXOffset; }
    /// @return room_y_offset = (MAP_SIZE - height)/2 (centring offset, y).
    int OffsetY() const { return m_RoomYOffset; }
    /// @return Rolled / supplied big-obstacle level in [1,3].
    int WallLevel() const { return m_WallLevel; }
    /// @return Rolled / supplied small-obstacle level in [1,3].
    int ObstacleLevel() const { return m_ObstacleLevel; }
    /// @return special_box_rate (0 normal, 20 badass) used in the render pass.
    int SpecialBoxRate() const { return m_SpecialBoxRate; }

    /**
     * @brief Read a build-grid cell.
     * @param x Column in [0, Width()).
     * @param y Row in [0, Height()).
     * @return The cell code (see the class cell-code legend).
     *
     * Out-of-range access returns 0 (defensive; the original would bounds-throw).
     */
    int At(int x, int y) const;

    /// @return The flat row-major grid (size Width()*Height(), index x*Height()+y).
    const std::vector<int> &Grid() const { return m_Map; }

    /**
     * @return The walkable floor cells (every cell whose final code is 0), in
     *         room-LOCAL coordinates as (x, y) pairs - exactly what the original
     *         appends to @c floor_list in CreateObstacle's render pass. The
     *         ordering matches the original's x-major, y-minor scan.
     */
    const std::vector<std::pair<int, int>> &FloorList() const { return m_FloorList; }

private:
    // --- Faithful pipeline stages (named after the original methods). ----------

    // FAITHFUL: RGRoomX__SetUpRoom @ rva 0x4FE234 (border stamp + stage order)
    void SetUpRoom();
    // FAITHFUL: RGRoomX__CreateAisle @ rva 0x4FCB74 (carve corridors + door cells)
    void CreateAisle();
    // FAITHFUL: RGRoomX__CreateFloor @ rva 0x4FD3E4 (3-pass noise/classify/variant)
    void CreateFloor();
    // FAITHFUL: RGRoomX__CreateWall @ rva 0x4FDB6C (perimeter decoration rolls)
    void CreateWall();
    // FAITHFUL: RGRoomX__CreateObstacle @ rva 0x4FE424 (big/small/render phases)
    void CreateObstacle();
    // FAITHFUL: RGRoomX__IsWallIntersect @ rva 0x4FF158 (footprint+1 margin clear)
    bool IsWallIntersect(int rx, int ry, int rw, int rh) const;

    // Stamp a big-obstacle footprint (ring->1, interior->-1, 1x1->8, 2x2->9).
    void StampBigObstacle(int px, int py, int w, int h);

    // Faithful map[x,y] accessors over the flat row-major (x*height+y) store.
    int Get(int x, int y) const;
    void Set(int x, int y, int value);

    RGRandom m_Rng;                  ///< rg_random @0x0C - the seeded stream.
    int m_RoomWidth = 0;             ///< room_width @0x14.
    int m_RoomHeight = 0;            ///< room_height @0x18.
    int m_WallLevel = 0;             ///< wall_level @0x20.
    int m_ObstacleLevel = 0;         ///< obstacle_level @0x24.
    std::array<int, 4> m_Entrance{}; ///< entrance[] @0x38.
    std::vector<int> m_Map;          ///< map @0x3C (flat, x*height+y).
    std::vector<std::pair<int, int>> m_FloorList; ///< floor_list @0x40.
    int m_RoomXOffset = 0;           ///< room_x_offset @0x70.
    int m_RoomYOffset = 0;           ///< room_y_offset @0x74.
    int m_SpecialBoxRate = 0;        ///< special_box_rate @0x78.
};

} // namespace Game

#endif /* GAME_ROOMGEN_HPP */
