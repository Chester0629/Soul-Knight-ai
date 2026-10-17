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

        // --- Phase 3 design-room interior (decision #2 = b'). Default OFF, so the
        // procedural path -- and every RoomGen golden -- is byte-identical. ---
        /// When false, CreateObstacle SKIPS the procedural big/small obstacle
        /// placement (Phase A/B) and instead stamps @ref designSolidCells; the
        /// floor-list render pass still runs. Set by the design-room loader for
        /// r1_* design slots so the prefab interior replaces procedural obstacles.
        bool proceduralObstacles = true;
        /// Room-LOCAL (x,y) interior cells to stamp as solid wall (code 1) in
        /// design mode -- the prefab's obj_index==0 wall markers. Only stamped
        /// onto currently-open floor cells (never door 11 / aisle -2 / border -1),
        /// so the RoomGen shell + 5-wide door band stay intact. Boxes/traps/pads
        /// are NOT here: they are a collision/trigger overlay the orchestrator
        /// builds, so a box blocks movement but never breaks door reachability
        /// (ConnectedFloorCells reads this grid; only obj_index-0 walls are barriers).
        std::vector<std::pair<int, int>> designSolidCells;

        /// TEST / EVIDENCE ONLY (P2.20), default false: reproduce the PRE-P2.20 draw sequence of CreateFloor
        /// pass 3 (1-2 draws per cell: a classification draw for edge in [1,2] or >= 4 plus an always-draw
        /// Range(2,4)) and of CreateWall (ONE roll per position). It exists so RoomDetailTest can show that
        /// the golden-grid changes of the P2.20 slice are caused by exactly that draw-order correction
        /// and nothing else (the old goldens come back with this flag set). FloorVariantAt /
        /// BorderCellDecorated carry no meaning in this mode.
        bool legacyDrawOrder = false;
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
    /// The floor tile VARIANT CreateFloor's pass 3 chose for a cell -- an index into
    /// MapManager.floor_list (0..5). FAITHFUL (P2.20, DISASM 0x50d8d0-0x50d9dc): one draw per cell,
    /// edge in [1,2] -> 0/1 (75/25), edge 0 or 3 -> 2/3 (50/50), edge >= 4 -> 5/4 (75/25).
    /// Out of range -> 0.
    int FloorVariantAt(int x, int y) const;

    /// The four sides of the perimeter wall ring CreateWall builds.
    enum class WallSide : int { South = 0, North = 1, West = 2, East = 3 };

    /// CreateWall's decoration threshold: a piece whose Range(0,100) roll is below this wears
    /// MapManager.wall_list[1] (DISASM `cmp r0,#0x1e`).
    static constexpr int kWallDecoThreshold = 30;
    /// The door-gap window in WORLD coordinates (18..22 inclusive): CreateWall places nothing and
    /// draws nothing there (DISASM `sub r2,r8,#0x12; cmp r2,#5; blo`).
    static constexpr int kGapLo = 18;
    static constexpr int kGapHi = 22;

    /// True when CreateWall rolled a value < kWallDecoThreshold for the perimeter piece of @p side
    /// at WORLD column (South/North) or row (West/East) @p along. Pieces in the door-gap window
    /// and out-of-range positions have no roll -> false.
    bool WallPieceDecorated(WallSide side, int along) const;
    /// True when CreateWall drew rolls for that piece (false in the gap window / out of range).
    bool WallPieceRolled(WallSide side, int along) const;
    /// PORT CHOICE (T-73): whether the port's border cell (x,y) wears the decorated wall -- the
    /// roll of the original piece in the same world column / row (see RoomGen.cpp). Interior cells
    /// and gap cells -> false.
    bool BorderCellDecorated(int x, int y) const;

    /// Number of RNG draws the build consumed so far (every Range call of the pipeline). The
    /// original's per-stage counts are pinned in RoomGenTest.
    long long DrawCount() const { return m_Draws; }

private:
    // --- Faithful pipeline stages (named after the original methods). ----------

    // FAITHFUL: RGRoomX__SetUpRoom @ rva 0x4FE234 (border stamp + stage order)
    void SetUpRoom();
    // FAITHFUL: RGRoomX__CreateAisle @ rva 0x4FCB74 (carve corridors + door cells)
    void CreateAisle();
    // FAITHFUL: RGRoomX__CreateFloor @ rva 0x4FD3E4 (3-pass noise/classify/variant)
    void CreateFloor();
    // FAITHFUL: RGRoomX__CreateWall @ rva 0x4FDB6C (two rolls per perimeter position; <30 -> wall_list[1])
    void CreateWall();
    /// Counting wrapper over RGRandom::Range -- every pipeline draw goes through it.
    int Roll(int minInclusive, int maxExclusive);
    /// Pass-3 variant draw for a pass-2 classification (one Roll).
    int FloorVariantFromEdge(int edge);
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
    std::vector<int> m_FloorVariant; ///< pass-3 variant per cell (x*height+y), 0..5.
    static constexpr signed char kWallNoRoll = -1;
    /// CreateWall result per side: 1 decorated / 0 plain / kWallNoRoll (gap window). South/North are
    /// indexed by world x - m_WallBaseX (x from room_x_offset-1), West/East by world y - m_WallBaseY.
    std::array<std::vector<signed char>, 4> m_WallDeco;
    int m_WallBaseX = 0;
    int m_WallBaseY = 0;
    long long m_Draws = 0;           ///< counted RNG draws.
    bool m_LegacyDrawOrder = false;  ///< Options::legacyDrawOrder (test / evidence only).
    int m_RoomXOffset = 0;           ///< room_x_offset @0x70.
    int m_RoomYOffset = 0;           ///< room_y_offset @0x74.
    int m_SpecialBoxRate = 0;        ///< special_box_rate @0x78.

    // Phase 3 design-room interior (port-only; not in the original RGRoomX field
    // layout -- these drive the design-obstacle branch of CreateObstacle).
    bool m_ProceduralObstacles = true;                   ///< Options.proceduralObstacles.
    std::vector<std::pair<int, int>> m_DesignSolidCells; ///< Options.designSolidCells (room-local).
};

} // namespace Game

#endif /* GAME_ROOMGEN_HPP */
