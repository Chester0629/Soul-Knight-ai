#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "world/RGMaze.hpp"

using Game::RGMaze;
using Game::RGPoint;

// NOLINTBEGIN(readability-magic-numbers)

namespace {

// Builds a fully walkable WxH grid (every cell == 0).
std::vector<int> OpenGrid(int w, int h) {
    return std::vector<int>(static_cast<std::size_t>(w) *
                                static_cast<std::size_t>(h),
                            0);
}

// Sets cell (x, y) to a wall value in a row-major grid of the given width.
void SetWall(std::vector<int> &grid, int w, int x, int y) {
    grid[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
         static_cast<std::size_t>(x)] = 1;
}

} // namespace

// --- Basic connectivity ------------------------------------------------------

TEST(RGMazeTest, StartEqualsEndReturnsStart) {
    RGMaze maze(OpenGrid(5, 5), 5, 5);
    RGPoint *result = maze.FindPath(RGPoint(2, 2), RGPoint(2, 2));
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->X, 2);
    EXPECT_EQ(result->Y, 2);
    // No iterations needed when start == end.
    EXPECT_EQ(maze.Times(), 0);
}

TEST(RGMazeTest, FindsPathOnOpenGrid) {
    RGMaze maze(OpenGrid(6, 1), 6, 1);
    RGPoint *goal = maze.FindPath(RGPoint(0, 0), RGPoint(4, 0));
    ASSERT_NE(goal, nullptr);
    EXPECT_EQ(goal->X, 4);
    EXPECT_EQ(goal->Y, 0);

    // Reconstructed path runs start -> goal and is contiguous.
    auto path = maze.ReconstructPath(goal);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front(), std::make_pair(0, 0));
    EXPECT_EQ(path.back(), std::make_pair(4, 0));
}

TEST(RGMazeTest, NoPathWhenWalledOff) {
    // 3x1 corridor with the middle cell walled: (0,0) is isolated from (2,0).
    // The reachable region drains the open list before the iteration cap, so
    // the faithful result is nullptr (no path).
    std::vector<int> grid = OpenGrid(3, 1);
    SetWall(grid, 3, 1, 0);
    RGMaze maze(std::move(grid), 3, 1);
    RGPoint *goal = maze.FindPath(RGPoint(0, 0), RGPoint(2, 0));
    EXPECT_EQ(goal, nullptr); // start and end are disconnected
}

// FAITHFUL QUIRK: when a goal is unreachable but the reachable region is large
// enough, the original does NOT cleanly return nullptr. Because FindMinFPoint
// selects the min-F node while FindPath always RemoveAt(0)s the FRONT, closed
// cells linger in the open list and get re-expanded, so the open list keeps
// accumulating entries instead of draining. The search then bails at the
// iteration cap and returns the current (partial) node rather than nullptr.
// This preserves the original's behavior exactly; it is why RGMaze is a
// connectivity heuristic, not a correct shortest-path solver.
TEST(RGMazeTest, DisconnectedLargeRegionBailsAtCapNotNull) {
    // 5x5 grid with column x==2 fully walled splits it into two halves.
    std::vector<int> grid = OpenGrid(5, 5);
    for (int y = 0; y < 5; ++y) {
        SetWall(grid, 5, 2, y);
    }
    RGMaze maze(std::move(grid), 5, 5);
    RGPoint *goal = maze.FindPath(RGPoint(0, 0), RGPoint(4, 0));
    ASSERT_NE(goal, nullptr);                 // does NOT return nullptr
    EXPECT_GT(maze.Times(), RGMaze::kMaxIterations); // bailed at the cap
    // The returned partial node lies in the reachable left half (X <= 1).
    EXPECT_LE(goal->X, 1);
}

TEST(RGMazeTest, NoPathWhenStartFullyEnclosed) {
    // Wall every orthogonal neighbour of the start cell.
    std::vector<int> grid = OpenGrid(5, 5);
    SetWall(grid, 5, 3, 2); // right of (2,2)
    SetWall(grid, 5, 1, 2); // left
    SetWall(grid, 5, 2, 3); // up
    SetWall(grid, 5, 2, 1); // down
    RGMaze maze(std::move(grid), 5, 5);
    RGPoint *goal = maze.FindPath(RGPoint(2, 2), RGPoint(4, 4));
    EXPECT_EQ(goal, nullptr);
}

// --- Step-cost (G) and heuristic (H / Manhattan) correctness -----------------

TEST(RGMazeTest, StepCostIsUnitOrthogonal) {
    // On a straight open corridor the goal's G equals the number of steps.
    RGMaze maze(OpenGrid(6, 1), 6, 1);
    RGPoint *goal = maze.FindPath(RGPoint(0, 0), RGPoint(3, 0));
    ASSERT_NE(goal, nullptr);
    EXPECT_EQ(goal->G, 3);            // 3 unit steps from start
    EXPECT_EQ(goal->H, 0);            // at the goal the heuristic is 0
    EXPECT_EQ(goal->F, goal->G + goal->H);
}

TEST(RGMazeTest, ManhattanHeuristicOnNeighbours) {
    // First expansion from start (0,0) toward end (3,0): the neighbour at (1,0)
    // gets G==1 and H==|3-1| + |0-0| == 2, so F==3.
    RGMaze maze(OpenGrid(6, 1), 6, 1);
    RGPoint *goal = maze.FindPath(RGPoint(0, 0), RGPoint(3, 0));
    ASSERT_NE(goal, nullptr);
    // Back-walk: every parent step decreases X by 1 and G by 1.
    int expectedG = goal->G;
    for (const RGPoint *p = goal; p != nullptr; p = p->ParentPoint) {
        EXPECT_EQ(p->G, expectedG);
        EXPECT_EQ(p->F, p->G + p->H);     // F == G + H invariant everywhere
        if (p->ParentPoint != nullptr) {
            // Non-start nodes get H via NotFoundPoint: Manhattan to (3,0).
            EXPECT_EQ(p->H, 3 - p->X);
        } else {
            // FAITHFUL: the start node is pushed directly (OpenList.Add(start))
            // with default cost fields, so it carries G==0, H==0, F==0 - it is
            // never run through NotFoundPoint/CalcH.
            EXPECT_EQ(p->X, 0);
            EXPECT_EQ(p->G, 0);
            EXPECT_EQ(p->H, 0);
            EXPECT_EQ(p->F, 0);
        }
        --expectedG;
    }
    EXPECT_EQ(expectedG, -1); // walked exactly G+1 nodes (G..0 inclusive)
}

// --- Iteration cap -----------------------------------------------------------

TEST(RGMazeTest, RespectsFiftyIterationCap) {
    // A large open grid with a far goal forces the search to run; the cap must
    // bound the iteration count and still return a (partial) node.
    const int n = 60;
    RGMaze maze(OpenGrid(n, n), n, n);
    RGPoint *result = maze.FindPath(RGPoint(0, 0), RGPoint(n - 1, n - 1));
    // The original returns the current node when times > 50, never nullptr here
    // (the grid is fully open, so the open list never empties first).
    ASSERT_NE(result, nullptr);
    EXPECT_LE(maze.Times(), RGMaze::kMaxIterations + 1);
    EXPECT_EQ(RGMaze::kMaxIterations, 50);
}

TEST(RGMazeTest, CapValueIsFifty) {
    EXPECT_EQ(RGMaze::kMaxIterations, 50);
}

// --- Determinism: same input -> identical output -----------------------------

TEST(RGMazeTest, DeterministicSameInputSameResult) {
    auto run = [] {
        std::vector<int> grid = OpenGrid(8, 8);
        SetWall(grid, 8, 3, 0);
        SetWall(grid, 8, 3, 1);
        SetWall(grid, 8, 3, 2);
        SetWall(grid, 8, 3, 3);
        SetWall(grid, 8, 3, 4);
        RGMaze maze(std::move(grid), 8, 8);
        RGPoint *goal = maze.FindPath(RGPoint(0, 0), RGPoint(7, 7));
        std::vector<std::pair<int, int>> path = maze.ReconstructPath(goal);
        return std::make_pair(maze.Times(), path);
    };

    auto a = run();
    auto b = run();
    EXPECT_EQ(a.first, b.first);   // identical iteration count
    EXPECT_EQ(a.second, b.second); // identical reconstructed path
}

TEST(RGMazeTest, ReuseAcrossCallsResetsState) {
    RGMaze maze(OpenGrid(6, 1), 6, 1);
    RGPoint *first = maze.FindPath(RGPoint(0, 0), RGPoint(4, 0));
    ASSERT_NE(first, nullptr);
    const int firstTimes = maze.Times();

    // A second identical call must produce identical state (no accumulation).
    RGPoint *second = maze.FindPath(RGPoint(0, 0), RGPoint(4, 0));
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(maze.Times(), firstTimes);
    EXPECT_EQ(second->X, 4);
    EXPECT_EQ(second->Y, 0);
    EXPECT_EQ(second->G, 4);
}

// --- Out-of-bounds is treated as wall ----------------------------------------

TEST(RGMazeTest, OutOfBoundsTreatedAsWall) {
    // A 1x1 grid: start == end is the only reachable cell; any other goal has
    // no in-bounds neighbours to expand into.
    RGMaze maze(OpenGrid(1, 1), 1, 1);
    RGPoint *same = maze.FindPath(RGPoint(0, 0), RGPoint(0, 0));
    ASSERT_NE(same, nullptr);
    EXPECT_EQ(same->X, 0);

    RGMaze maze2(OpenGrid(1, 1), 1, 1);
    RGPoint *unreachable = maze2.FindPath(RGPoint(0, 0), RGPoint(5, 5));
    EXPECT_EQ(unreachable, nullptr); // nowhere to expand -> open list empties
}

// NOLINTEND(readability-magic-numbers)
