#ifndef GAME_FLOOR_BLOCK_HPP
#define GAME_FLOOR_BLOCK_HPP

#include <array>
#include <utility>
#include <vector>

#include "world/RGAisle.hpp"
#include "world/RoomGen.hpp"

namespace Game {

/**
 * @class FloorBlock
 * @brief Places one generated @ref RoomGen room into its faithful @c MAP_SIZE
 *        (41x41) dungeon block and carves the inter-room corridors.
 *
 * @par Why this exists ("A geometry")
 * @ref RoomGen builds only the room itself (a @c Width()xHeight() grid, e.g.
 * 15x15). The faithful original, however, allocates each room a full
 * @c MAP_SIZE x MAP_SIZE logical block (decomp @c 0x29 = 41) and centres the
 * room inside it via @c room_x_offset / @c room_y_offset = @c (41-size)/2 (the
 * offset @ref RoomGen already computes). The margin between the room edge and
 * the block edge is where the corridor that links two adjacent rooms lives. With
 * every room occupying a 41-cell block, blocks tile at a fixed pitch of
 * @c 41 * cellPx ( = 1312 px at 32 px/cell), and two grid-adjacent rooms' blocks
 * sit edge-to-edge so their corridors meet at the shared seam (this room's last
 * corridor cell, block index 40, is orthogonally adjacent to the neighbour
 * block's first cell, index 0 of the next 41-block).
 *
 * @par Corridor geometry (RGAisle-math)
 * Each corridor is the fixed @ref RGAisle::kAisleShortExtent (5) cells wide on
 * its short axis, aligned to the room's door opening as @ref RoomGen::CreateAisle
 * actually carves it (the @c cy..cy+4 / @c cx..cx+4 aisle band -- that carve, not
 * @ref RGAisle::GridDims, is the authority on which axis/band the door sits, since
 * GridDims and CreateAisle index different axes for EAST/SOUTH and only coincide
 * for square rooms). The corridor runs from the room edge out to the block edge
 * along the long axis: the whole room offset = the junction cell at the room edge
 * plus @c offset-1 corridor-floor cells. For the fixed 15-cell room that is 13
 * cells (junction at block index 28 + floor 29..40).
 *
 * Pure and engine-free: the cell grid it returns is the single source of truth
 * for both the connectivity unit tests (BFS over the walkable set) and the
 * GameScene tile / collider build, so the tested walkable space is exactly the
 * in-game walkable space.
 */
class FloorBlock {
public:
    /// The dungeon block is MAP_SIZE x MAP_SIZE cells (decomp 0x29 = 41).
    static constexpr int kBlock = RoomGen::MAP_SIZE;

    /// Corridor short-axis width in cells (faithful RGAisle short extent).
    static constexpr int kCorridorWidth = RGAisle::kAisleShortExtent;

    /// Direction codes (RGRoomX aisle ordering == RoomGen entrance index):
    ///   0 = EAST(+x), 1 = (-y), 2 = WEST(-x), 3 = (+y).
    static constexpr int DIR_EAST = 0;
    static constexpr int DIR_NEG_Y = 1;
    static constexpr int DIR_WEST = 2;
    static constexpr int DIR_POS_Y = 3;

    /// Walkable corridor cell code stamped into the margin (aisle, walkable).
    static constexpr int kCorridorCell = -2;
    /// Solid fill for the non-corridor margin (border, blocks movement).
    static constexpr int kMarginCell = -1;

    /// @return block-local x of the room's cell (0,0): (kBlock - Width())/2.
    static int OffsetX(const RoomGen &rg);
    /// @return block-local y of the room's cell (0,0): (kBlock - Height())/2.
    static int OffsetY(const RoomGen &rg);

    /**
     * @struct Rect
     * @brief An inclusive block-local cell rectangle [x0,x1] x [y0,y1].
     *        Empty when x1 < x0 or y1 < y0.
     */
    struct Rect {
        int x0 = 0;
        int y0 = 0;
        int x1 = -1;
        int y1 = -1;
        bool Empty() const { return x1 < x0 || y1 < y0; }
    };

    /**
     * @brief The 5-wide walkable corridor strip for @p dir, block-local.
     * @param dir One of DIR_EAST / DIR_NEG_Y / DIR_WEST / DIR_POS_Y.
     * @param rg  The room (for its size / offsets / door band).
     * @return The inclusive strip from the room edge out to the block edge,
     *         aligned to the room's @c cy..cy+4 (E/W) or @c cx..cx+4 (N/S) door
     *         band. This is the SINGLE source of truth shared by @ref Build and
     *         GameScene's corridor tile/collider build.
     */
    static Rect CorridorStrip(int dir, const RoomGen &rg);

    /**
     * @brief Build the kBlock x kBlock cell grid for @p rg in its dungeon block.
     * @param rg       The generated room.
     * @param entrance Door flags per direction (index == dir): 1 carves the
     *                 corridor toward that neighbour out to the block edge.
     * @return Flat row-major grid (index @c x*kBlock + y, matching RoomGen):
     *         the margin is @ref kMarginCell, the room is stamped at
     *         (OffsetX,OffsetY), and each set entrance's @ref CorridorStrip is
     *         carved as @ref kCorridorCell.
     */
    static std::vector<int> Build(const RoomGen &rg,
                                  const std::array<int, 4> &entrance);

    /// Bounds-safe read of a built block grid; OOB returns @ref kMarginCell.
    static int At(const std::vector<int> &block, int x, int y);

    /**
     * @brief The room's floor cells that are reachable from a corridor mouth.
     * @param rg The generated room (its carved aisle/door cells are the mouths).
     * @return Room-LOCAL (x,y) floor cells (final code 0) that flood-fill-connect
     *         to at least one of the room's aisle(-2)/door(11) cells, in x-major
     *         scan order.
     *
     * @ref RoomGen::FloorList contains EVERY floor cell, including ones an
     * obstacle layout can isolate into a pocket -- spawning the player there (the
     * start room) would strand them, and a naive interior cell would fail a
     * corridor reachability test for the wrong reason. This filters @c FloorList
     * to the door-connected main region, so any cell it returns is reachable
     * through the corridor to every adjacent room. GameScene spawns the player /
     * enemies on these cells and the connectivity test anchors on them, keeping
     * the tested interior and the in-game interior one and the same. Falls back to
     * the full @c FloorList only for a room with no carved aisle (no neighbour).
     */
    static std::vector<std::pair<int, int>>
    ConnectedFloorCells(const RoomGen &rg);

    /**
     * @brief The room-LOCAL perimeter cells that form the full door opening.
     * @param rg The generated room.
     * @return Every cell on the room boundary (x==0 || x==Width()-1 || y==0 ||
     *         y==Height()-1) whose code is walkable (aisle -2 or door 11), in
     *         x-major scan order.
     *
     * A clear-room lock must seal the WHOLE door opening, not just the two code-11
     * flank cells: @ref RoomGen::CreateAisle carves the opening as a 5-wide aisle
     * band (code -2) plus two flanking door cells (code 11), so sealing only the
     * code-11 cells leaves the 160px aisle band open and the locked room contains
     * nobody. GameScene builds its per-room door colliders from this (the cells
     * sealed while a room is the active uncleared room).
     */
    static std::vector<std::pair<int, int>> DoorSealCells(const RoomGen &rg);
};

} // namespace Game

#endif /* GAME_FLOOR_BLOCK_HPP */
