#include "world/RGMaze.hpp"

#include <cstddef>
#include <utility>

namespace Game {

// FAITHFUL: RGMaze___ctor @ rva 0x4F3C4C
RGMaze::RGMaze(std::vector<int> maze, int width, int height)
    : m_Map(std::move(maze)), m_Width(width), m_Height(height) {}

RGPoint *RGMaze::NewNode(int x, int y) {
    m_Nodes.push_back(std::make_unique<RGPoint>(x, y));
    return m_Nodes.back().get();
}

bool RGMaze::Walkable(int x, int y) const {
    // The original assumes a bordered grid; we guard OOB (treat as wall) to
    // avoid the il2cpp bounds-throw while preserving the cell==0 walkable rule.
    if (x < 0 || y < 0 || x >= m_Width || y >= m_Height) {
        return false;
    }
    const std::size_t idx =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(m_Width) +
        static_cast<std::size_t>(x);
    return m_Map[idx] == 0;
}

// FAITHFUL: RGMaze__FindPath @ rva 0x4F3D00
RGPoint *RGMaze::FindPath(RGPoint start, RGPoint end) {
    // Reset per-search state so the maze can be reused across calls.
    m_OpenList.clear();
    m_CloseList.clear();
    m_Nodes.clear();
    m_Times = 0;

    RGPoint *startNode = NewNode(start.X, start.Y);
    RGPoint *endNode = NewNode(end.X, end.Y);

    m_OpenList.push_back(startNode);
    if (startNode->X == endNode->X && startNode->Y == endNode->Y) {
        return startNode;
    }

    while (true) {
        if (m_OpenList.empty()) {
            return nullptr; // no path
        }

        RGPoint *current = FindMinFPoint(); // lowest-F node
        // QUIRK: the original removes the FRONT of the open list each iteration
        // (RemoveAt(0)), not necessarily the `current` node it just selected.
        m_OpenList.erase(m_OpenList.begin());
        m_CloseList.push_back(current);

        for (RGPoint *nb : SurrroundPoints(current)) {
            bool inOpen = false; // already queued? (match X && Y)
            for (RGPoint *p : m_OpenList) {
                if (p->X == nb->X) {
                    inOpen = inOpen || (p->Y == nb->Y);
                }
            }

            if (inOpen) {
                FoundPoint(current, nb); // effectively a no-op (see header)
            } else {
                NotFoundPoint(current, endNode, nb);
            }
        }

        m_Times++;
        if (m_Times > kMaxIterations) {
            return current; // hard iteration cap (0x32)
        }

        for (RGPoint *p : m_OpenList) { // goal reached?
            if (p->X == endNode->X && p->Y == endNode->Y) {
                return p;
            }
        }
    }
}

// FAITHFUL: RGMaze__FindMinFPoint @ rva 0x4F43C4
// Linear scan for the lowest-F node in OpenList (strictly-less keeps the first).
RGPoint *RGMaze::FindMinFPoint() {
    RGPoint *best = m_OpenList[0];
    int bestF = best->F;
    for (std::size_t i = 1; i < m_OpenList.size(); ++i) {
        RGPoint *p = m_OpenList[i];
        if (p->F < bestF) {
            bestF = p->F;
            best = p;
        }
    }
    return best;
}

// FAITHFUL: RGMaze__SurrroundPoints @ rva 0x4F4518
// Fresh RGPoints for the 4 orthogonal neighbours whose maze cell == 0.
// Probe order matches the original: +X, -X, +Y, -Y.
std::vector<RGPoint *> RGMaze::SurrroundPoints(const RGPoint *point) {
    std::vector<RGPoint *> list;
    const int x = point->X;
    const int y = point->Y;
    if (Walkable(x + 1, y)) {
        list.push_back(NewNode(x + 1, y));
    }
    if (Walkable(x - 1, y)) {
        list.push_back(NewNode(x - 1, y));
    }
    if (Walkable(x, y + 1)) {
        list.push_back(NewNode(x, y + 1));
    }
    if (Walkable(x, y - 1)) {
        list.push_back(NewNode(x, y - 1));
    }
    return list;
}

// FAITHFUL: RGMaze__CalcG @ rva 0x4F4AC0
// G of a node = parent.G + 1 (unit step cost), or 1 if no parent.
int RGMaze::CalcG(const RGPoint *point) {
    return (point->ParentPoint != nullptr) ? point->ParentPoint->G + 1 : 1;
}

// FAITHFUL: RGMaze__CalcH @ rva 0x4F4B1C
// Manhattan heuristic |dx| + |dy| (the original's first arg is unused).
int RGMaze::CalcH(const RGPoint *end, const RGPoint *point) {
    int dx = point->X - end->X;
    if (dx < 0) {
        dx = -dx;
    }
    int dy = point->Y - end->Y;
    if (dy < 0) {
        dy = -dy;
    }
    return dx + dy;
}

// FAITHFUL: RGMaze__FoundPoint @ rva 0x4F4998
// Relax `point` only if cheaper. SurrroundPoints yields FRESH nodes
// (ParentPoint == null, G == 0), so g == 1 and (point->G == 0 <= 1) always
// holds: for neighbours already in OpenList this is effectively a skip.
void RGMaze::FoundPoint(RGPoint *tempStart, RGPoint *point) {
    const int g =
        (point->ParentPoint != nullptr) ? point->ParentPoint->G + 1 : 1;
    if (point->G <= g) {
        return;
    }
    point->ParentPoint = tempStart;
    point->G = g;
    point->F = point->H + g;
}

// FAITHFUL: RGMaze__NotFoundPoint @ rva 0x4F49EC
// Register a new node: parent = current, G = current.G + 1, H = Manhattan,
// F = G + H, then push onto OpenList.
void RGMaze::NotFoundPoint(RGPoint *tempStart, const RGPoint *end,
                           RGPoint *point) {
    point->ParentPoint = tempStart;
    point->G = (tempStart != nullptr) ? tempStart->G + 1 : 1;
    point->H = CalcH(end, point);
    point->F = point->G + point->H;
    m_OpenList.push_back(point);
}

std::vector<std::pair<int, int>>
RGMaze::ReconstructPath(const RGPoint *goal) const {
    std::vector<std::pair<int, int>> path;
    for (const RGPoint *p = goal; p != nullptr; p = p->ParentPoint) {
        path.emplace_back(p->X, p->Y);
    }
    // Back-walk yields goal -> start; reverse to forward (start -> goal) order.
    for (std::size_t i = 0, j = path.size(); i + 1 < j; ++i, --j) {
        std::swap(path[i], path[j - 1]);
    }
    return path;
}

} // namespace Game
