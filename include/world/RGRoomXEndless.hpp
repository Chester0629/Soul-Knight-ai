#ifndef GAME_RGROOMXENDLESS_HPP
#define GAME_RGROOMXENDLESS_HPP

#include <vector>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RGRoomXEndless
 * @brief Pure-geometry AABB Rect-overlap validity test from Soul Knight
 *        1.7.10's @c RGRoomXEndless (the Endless-mode room variant).
 *
 * Faithful port of @c RGRoomXEndless__IsOccupied. This is the recoverable,
 * self-contained decision unit the completeness sweep flagged as gap G8.
 *
 * @par What this is (and is NOT)
 * @c IsOccupied is the Endless room's obstacle-placement validity check. It is
 * DISTINCT from @c RoomGen::IsWallIntersect (the grid-CELL @c map==0 scan ported
 * in RoomGen.cpp). Where @c IsWallIntersect walks the integer build grid, the
 * Endless variant tests a query @c Rect for geometric AABB overlap against a
 * runtime @c List<Rect> of already-placed obstacle footprints (decomp field at
 * @c this+0x7c). That list is populated owner-side as obstacles are placed, so
 * here it is modelled as an explicit input (@ref PlacedRect collection).
 *
 * @par Query rectangle (the 1-cell padding)
 * For an obstacle candidate at cell @c (col,row) of size @c (w,h), the decomp
 * builds the query @c Rect with a 1-cell margin on every side:
 *   @c (x = col-1, y = row-1, width = w+2, height = h+2).
 * It then scans the placed-rect list and returns @c true on the FIRST rect that
 * overlaps the padded query, else @c false. The owner uses a @c false result as
 * "this cell is free" (see @c RGRoomXEndless__CreateObstacle @ game_full.c:429150,
 * which only places when @c IsOccupied returns 0).
 *
 * @par Faithfulness
 * @c IsOccupied draws NO RNG and writes NO state - it is a pure predicate over
 * its inputs. The owner-side @c CreateObstacle body (Instantiate / MapManager /
 * List.Add tails) is NOT recoverable as pure logic and is intentionally omitted;
 * only this geometric predicate is ported. There are no fabricated constants:
 * the only literals are the @c -1 / @c +2 padding and the @c x/y/width/height
 * Rect accessors, all read directly from the decompilation.
 */
class RGRoomXEndless {
public:
    /**
     * @struct PlacedRect
     * @brief One already-placed obstacle footprint from the runtime
     *        @c List<Rect> at @c RGRoomXEndless+0x7c.
     *
     * Mirrors a UnityEngine.Rect: @c x / @c y are the min corner, @c width /
     * @c height the extents, so the rect spans @c [x, x+width] x [y, y+height].
     * These are float because the decomp reads them through the Rect float
     * accessors (@c get_x / @c get_y / @c get_width / @c get_height); placement
     * passes integer cell coordinates, but the overlap test is done in float.
     */
    struct PlacedRect {
        float x = 0.0F;      ///< Rect.x   (FUN_00bf79a8 get_x).
        float y = 0.0F;      ///< Rect.y   (FUN_00bf79c8 get_y).
        float width = 0.0F;  ///< Rect.width  (FUN_00bf7db8 get_width).
        float height = 0.0F; ///< Rect.height (FUN_00bf7dd8 get_height).
    };

    /**
     * @brief Test whether a candidate obstacle cell overlaps any placed rect.
     *
     * @param col   Candidate cell column (decomp @c param_2).
     * @param row   Candidate cell row    (decomp @c param_3).
     * @param w     Candidate footprint width  (decomp @c param_4).
     * @param h     Candidate footprint height (decomp @c param_5).
     * @param placed The runtime placed-obstacle @c List<Rect> (decomp @c +0x7c),
     *               supplied as an input since the owner populates it.
     *
     * @return @c true if the padded query rect @c (col-1,row-1,w+2,h+2) overlaps
     *         at least one placed rect (decomp @c return 1), @c false otherwise
     *         (decomp @c return 0). Returns on the FIRST overlap found.
     *
     * FAITHFUL: RGRoomXEndless__IsOccupied @ game_full.c:429286
     */
    static bool IsOccupied(int col, int row, int w, int h,
                           const std::vector<PlacedRect> &placed);
};

} // namespace Game

#endif /* GAME_RGROOMXENDLESS_HPP */
