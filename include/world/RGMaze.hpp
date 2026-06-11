#ifndef GAME_RGMAZE_HPP
#define GAME_RGMAZE_HPP

#include <memory>
#include <utility>
#include <vector>

namespace Game {

/**
 * @struct RGPoint
 * @brief A single cell node in the @ref RGMaze A* search.
 *
 * Faithful field-by-field port of the original Soul Knight 1.7.10 @c RGPoint
 * (IL2CPP layout: ParentPoint\@0x08, F\@0x0C, G\@0x10, H\@0x14, X\@0x18,
 * Y\@0x1C). @c ParentPoint is the back-pointer used to reconstruct the path
 * once the goal is reached.
 *
 * @note Ownership: nodes are heap-allocated and owned by the @ref RGMaze that
 *       created them (see @ref RGMaze::Nodes). @c ParentPoint is a non-owning
 *       observer into that same pool, so back-walking a returned node is safe
 *       for as long as the owning @ref RGMaze is alive.
 */
struct RGPoint {
    RGPoint *ParentPoint = nullptr; ///< 0x08 back-pointer for path reconstruction.
    int F = 0;                      ///< 0x0C total cost (G + H).
    int G = 0;                      ///< 0x10 cost so far from start.
    int H = 0;                      ///< 0x14 Manhattan heuristic to goal.
    int X = 0;                      ///< 0x18 grid column.
    int Y = 0;                      ///< 0x1C grid row.

    RGPoint() = default;

    /**
     * @brief Constructs a node at grid coordinate (@p x, @p y).
     *
     * Mirrors the original @c RGPoint(int x, int y) constructor: only X and Y
     * are set, all cost fields start at 0 and @c ParentPoint at null.
     */
    RGPoint(int x, int y) : X(x), Y(y) {}
};

/**
 * @class RGMaze
 * @brief 4-directional grid A* used for room-connectivity validation.
 *
 * Faithful port of Soul Knight 1.7.10 @c RGMaze (FindPath \@ rva 0x4F3D00).
 * The grid is a row-major integer array where a cell value of @c 0 is walkable
 * and any non-zero value is a wall. Movement is orthogonal only (4-neighbour),
 * each step costs @c 1 (G), the heuristic is Manhattan distance (H), and the
 * evaluated cost is @c F = G + H. The open set is scanned linearly for the
 * minimum-F node (not a binary heap), and a hard iteration cap of @c 50
 * bounds the search.
 *
 * The purpose is connectivity checking, not optimal navigation, so the
 * original's observable quirks are preserved deliberately:
 *  - @c FindPath removes the FRONT of the open list each iteration
 *    (@c RemoveAt(0)) rather than the actual minimum-F node it selected.
 *  - Neighbours already present in the open list are passed to @c FoundPoint,
 *    which never relaxes them (fresh neighbour nodes always have @c G == 0,
 *    so the @c G <= g guard always holds): an effective no-op.
 *  - When @c times exceeds 50 the search bails and returns the current node
 *    (a partial/heuristic answer), not @c nullptr.
 *
 * @see D:/Soul Knight/_reverse/recreation/Dungeon/RGMaze.cs
 */
class RGMaze {
public:
    /**
     * @brief Wraps a maze grid for pathfinding.
     *
     * @param maze   Row-major grid stored as @c maze[y * width + x]; cell
     *               value @c 0 means walkable, non-zero means wall.
     * @param width  Number of columns (X span). Must be > 0.
     * @param height Number of rows (Y span). Must be > 0.
     *
     * The grid is copied so the maze owns a stable view for the search.
     */
    RGMaze(std::vector<int> maze, int width, int height);

    /**
     * @brief Runs the A* search from @p start to @p end.
     *
     * @param start Start coordinate (must be walkable to make progress).
     * @param end   Goal coordinate.
     * @return The goal node (back-walk @c ParentPoint to reconstruct the path),
     *         or @c nullptr when no path exists. If the 50-iteration cap is hit
     *         first, the current node is returned (faithful to the original).
     *
     * @note The returned pointer (and its @c ParentPoint chain) is owned by
     *       this @ref RGMaze and remains valid until the maze is destroyed or
     *       @ref FindPath is called again.
     */
    RGPoint *FindPath(RGPoint start, RGPoint end);

    /**
     * @brief Reconstructs the cell path by back-walking @c ParentPoint.
     *
     * @param goal A node returned by @ref FindPath (may be @c nullptr).
     * @return The path from start to @p goal as (x, y) pairs in forward order.
     *         Empty when @p goal is @c nullptr.
     */
    std::vector<std::pair<int, int>> ReconstructPath(const RGPoint *goal) const;

    /// @return Number of search iterations performed by the last @ref FindPath.
    int Times() const { return m_Times; }

    /// @return Grid width (columns / X span).
    int Width() const { return m_Width; }

    /// @return Grid height (rows / Y span).
    int Height() const { return m_Height; }

    /// Hard iteration cap (0x32). times > 50 bails the search.
    static constexpr int kMaxIterations = 50;

private:
    // --- Faithful helpers (named after the original methods). ----------------

    // FAITHFUL: RGMaze__FindMinFPoint @ rva 0x4F43C4
    RGPoint *FindMinFPoint();

    // FAITHFUL: RGMaze__SurrroundPoints @ rva 0x4F4518 (note original triple-r typo)
    std::vector<RGPoint *> SurrroundPoints(const RGPoint *point);

    // FAITHFUL: RGMaze__CalcG @ rva 0x4F4AC0
    static int CalcG(const RGPoint *point);

    // FAITHFUL: RGMaze__CalcH @ rva 0x4F4B1C
    static int CalcH(const RGPoint *end, const RGPoint *point);

    // FAITHFUL: RGMaze__FoundPoint @ rva 0x4F4998
    static void FoundPoint(RGPoint *tempStart, RGPoint *point);

    // FAITHFUL: RGMaze__NotFoundPoint @ rva 0x4F49EC
    void NotFoundPoint(RGPoint *tempStart, const RGPoint *end, RGPoint *point);

    // Allocates an owned node and returns a stable pointer to it.
    RGPoint *NewNode(int x, int y);

    // True when (x, y) is inside the grid and the cell is walkable (== 0).
    bool Walkable(int x, int y) const;

    std::vector<int> m_Map;                       ///< 0x08 maze_map (row-major).
    int m_Width = 0;                              ///< Columns (X span).
    int m_Height = 0;                             ///< Rows (Y span).
    std::vector<RGPoint *> m_CloseList;           ///< 0x0C CloseList.
    std::vector<RGPoint *> m_OpenList;            ///< 0x10 OpenList.
    int m_Times = 0;                              ///< 0x14 times (iteration count).
    std::vector<std::unique_ptr<RGPoint>> m_Nodes; ///< Backing storage for nodes.
};

} // namespace Game

#endif /* GAME_RGMAZE_HPP */
