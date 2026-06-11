#ifndef GAME_RGAISLE_HPP
#define GAME_RGAISLE_HPP

#include <array>
#include <cstddef>
#include <vector>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RGAisle
 * @brief Faithful port of the recoverable pure-logic in Soul Knight 1.7.10's
 *        @c RGAisle corridor builder (the strip that links two adjacent rooms).
 *
 * @par Scope - what is and is NOT ported
 * @c RGAisle has four native methods. Most of the class is owner-side (Unity
 * @c Transform positioning + tile @c Instantiate); only @c CreateFloor's first
 * two grid passes and @c CreateAisleWall's gated RNG cadence are recoverable
 * pure logic:
 *  - @c RGAisle__CreateAisle  @ game_full.c:390206 - OWNER-SIDE. A @c switch on
 *    @c direction (offset 0x18) that, per non-default case, calls
 *    @c get_transform + a non-returning register-only helper (FUN_010b7dcc);
 *    the default case delegates to CreateFloor + CreateAisleWall. No RNG, no
 *    pure state. Modelled only as the documented sub-step order.
 *  - @c RGAisle__CreateFloor  @ game_full.c:390250 - the core. Allocates a
 *    @c w x h int grid whose dimensions come from @c direction + the owning
 *    room's offsets, then runs three passes. Pass 1 (noise) and pass 2 (edge
 *    classification) are clean and ported here. Pass 3 (variant pick) is
 *    OWNER-SIDE: the decompiler corrupted its loop bound (the row-count
 *    register is clobbered by scratch reuse) and elided the column loop, and
 *    every per-cell draw exists solely to index a floor-tile prefab that is
 *    immediately @c Instantiate-d. See @ref FloorDrawsAreOwnerSide.
 *  - @c RGAisle__CreateAisleWall @ game_full.c:390523 - a gated 2x
 *    @c Range(0,100) cadence; ported as @ref RollAisleWall.
 *  - @c RGAisle__CreateWall   @ game_full.c:390614 - OWNER-SIDE. Just
 *    @c get_gameObject on the wall-group transform. Not modelled.
 *
 * All randomness flows through one seeded @ref RGRandom stream, so the same seed
 * (and same direction + room offsets) yields a bit-identical noise/edge grid and
 * an identical draw stream - the determinism the original relies on for
 * networked replay.
 *
 * @par Direction encoding (from RGRoomX aisle_n/e/w/s ordering)
 *  - @c 0 = NORTH, @c 1 = EAST, @c 2 = WEST, @c 3 = SOUTH.
 * The decomp selector @c (direction | 2) == 2 is true for NORTH(0) and WEST(2);
 * those run a @c (roomXOffset-1) x 5 grid, while EAST(1)/SOUTH(3) run a
 * @c 5 x (roomYOffset-1) grid.
 */
class RGAisle {
public:
    /// Direction codes (RGRoomX aisle_n/e/w/s ordering); @c direction @0x18.
    static constexpr int DIR_NORTH = 0;
    static constexpr int DIR_EAST = 1;
    static constexpr int DIR_WEST = 2;
    static constexpr int DIR_SOUTH = 3;

    /// The fixed corridor extent on its short axis (decomp literal 5).
    static constexpr int kAisleShortExtent = 5;

    /// Pass-1 noise is a coin flip per cell: Range(0, kNoiseCeiling) (exclusive).
    static constexpr int kNoiseCeiling = 2;

    /// CreateAisleWall rolls Range(0, kWallRollCeiling) (exclusive) per draw.
    static constexpr int kWallRollCeiling = 100;

    /// CreateAisleWall picks prefab index 1 when the first roll is < this, else 0.
    static constexpr int kWallVariantThreshold = 6;

    /**
     * @struct Dims
     * @brief The aisle build-grid dimensions derived by CreateFloor.
     *
     * One axis is always @ref kAisleShortExtent; the other is the owning room's
     * matching offset minus 1 (the corridor length out to the dungeon edge).
     */
    struct Dims {
        int width = 0;  ///< x span (cells).
        int height = 0; ///< y span (cells).
    };

    /// Construct an unseeded aisle brain; seed before any draw with @ref SetSeed.
    RGAisle() = default;

    /**
     * @brief Seed the deterministic stream (wraps RGRandom::SetRandomSeed).
     * @param seed Network-authoritative seed in the original.
     */
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }

    /// @return true once @ref SetSeed has been called.
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Compute the aisle build-grid dimensions for a direction/room.
     * @param direction One of DIR_NORTH/EAST/WEST/SOUTH.
     * @param roomXOffset The owning room's @c room_x_offset (@0x70).
     * @param roomYOffset The owning room's @c room_y_offset (@0x74).
     * @return @ref Dims: NORTH/WEST -> {roomXOffset-1, 5}; EAST/SOUTH ->
     *         {5, roomYOffset-1}. No RNG.
     *
     * FAITHFUL: RGAisle__CreateFloor @ game_full.c:390274-390289 (dim selector).
     */
    static Dims GridDims(int direction, int roomXOffset, int roomYOffset);

    /**
     * @brief Run CreateFloor's pass 1 (noise) + pass 2 (edge classification).
     * @param dims The grid dimensions from @ref GridDims.
     * @return The flat row-major (index x*height + y) edge-classification grid,
     *         exactly as the original computes it before the (owner-side) pass 3.
     *
     * Pass 1 draws @c width*height x @c Range(0,2) noise; pass 2 takes NO draw,
     * classifying each cell by its empty (==0) orthogonal neighbours with the
     * decomp's weighted up-neighbour counting. The stream is left positioned
     * exactly where pass 2 ends - the owner's pass-3 placement loop continues
     * from here (see @ref FloorDrawsAreOwnerSide).
     *
     * FAITHFUL: RGAisle__CreateFloor @ game_full.c:390290-390424 (pass 1 + 2).
     */
    std::vector<int> BuildFloorEdges(const Dims &dims);

    /**
     * @struct WallRoll
     * @brief Outcome of CreateAisleWall's gated decoration roll.
     */
    struct WallRoll {
        bool placed = false;     ///< true iff the aisle length gate passed.
        int prefabIndex = 0;     ///< 0/1 wall-tile prefab index (firstRoll < 6).
        int firstRoll = -1;      ///< the first Range(0,100) value (-1 = no draw).
        int secondRoll = -1;     ///< the second Range(0,100) value (-1 = no draw).
    };

    /**
     * @brief Port of CreateAisleWall's gated 2x Range(0,100) cadence.
     * @param direction One of DIR_NORTH/EAST/WEST/SOUTH.
     * @param roomXOffset The owning room's @c room_x_offset (@0x70).
     * @param roomYOffset The owning room's @c room_y_offset (@0x74).
     * @return @ref WallRoll. When the aisle length (offset-1 on the active axis)
     *         is <= 0 the gate fails: NO draw is taken (placed=false). Otherwise
     *         two Range(0,100) draws happen; the first selects a 0/1 prefab index
     *         (firstRoll < 6 -> index 1, else 0) and the second only advances the
     *         stream (its value is otherwise unused by the original).
     *
     * FAITHFUL: RGAisle__CreateAisleWall @ game_full.c:390523.
     */
    WallRoll RollAisleWall(int direction, int roomXOffset, int roomYOffset);

    /**
     * @brief Documentation marker: CreateFloor's pass 3 is owner-side.
     *
     * The decompiler corrupted pass 3 (game_full.c:390425-390513): the row-count
     * register @c uVar11 is overwritten by array-stride / variant-result scratch
     * inside the loop body, so the iteration count is unrecoverable, and the
     * column loop was elided. Every per-cell draw there feeds @c GetInstance ->
     * @c Instantiate<floor-tile> -> @c get_transform (a non-returning owner
     * call). Modelling it would require guessing the loop bound and would emit
     * draws the orchestrator owns. We therefore stop at pass 2.
     * @return Always false (this is a compile-time documentation hook only).
     */
    static constexpr bool FloorDrawsAreOwnerSide() { return false; }

private:
    /// Flat row-major index (faithful map[x,y] = x*height + y layout).
    static std::size_t Index(int x, int y, int height);

    RGRandom m_Rng; ///< rg_random stream (seeded via SetSeed).
};

} // namespace Game

#endif /* GAME_RGAISLE_HPP */
