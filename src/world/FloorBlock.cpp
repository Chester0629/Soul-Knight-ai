#include "world/FloorBlock.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace Game {

int FloorBlock::OffsetX(const RoomGen &rg) { return (kBlock - rg.Width()) / 2; }
int FloorBlock::OffsetY(const RoomGen &rg) { return (kBlock - rg.Height()) / 2; }

int FloorBlock::At(const std::vector<int> &block, int x, int y) {
    if (x < 0 || y < 0 || x >= kBlock || y >= kBlock) {
        return kMarginCell;
    }
    const std::size_t idx =
        static_cast<std::size_t>(x) * static_cast<std::size_t>(kBlock) +
        static_cast<std::size_t>(y);
    if (idx >= block.size()) {
        return kMarginCell;
    }
    return block[idx];
}

// The 5-wide corridor strip for a direction, block-local. The short axis is
// aligned to RoomGen::CreateAisle's carved aisle band (cx..cx+4 / cy..cy+4); the
// long axis runs from the room edge out to the block edge (the room's full
// room_x/y_offset: the junction cell + the offset-1 corridor-floor cells, which
// matches RGAisle::GridDims's offset-1 length plus the junction).
FloorBlock::Rect FloorBlock::CorridorStrip(int dir, const RoomGen &rg) {
    const int ox = OffsetX(rg);
    const int oy = OffsetY(rg);
    const int w = rg.Width();
    const int h = rg.Height();
    const int cx = (w - 5) / 2; // RoomGen::CreateAisle cx
    const int cy = (h - 5) / 2; // RoomGen::CreateAisle cy

    Rect r;
    switch (dir) {
    case DIR_EAST: // +x: room east edge (ox+w) out to the block edge.
        r.x0 = ox + w;
        r.x1 = kBlock - 1;
        r.y0 = oy + cy;
        r.y1 = oy + cy + kCorridorWidth - 1;
        break;
    case DIR_WEST: // -x: block edge (0) up to the room west edge.
        r.x0 = 0;
        r.x1 = ox - 1;
        r.y0 = oy + cy;
        r.y1 = oy + cy + kCorridorWidth - 1;
        break;
    case DIR_NEG_Y: // -y: block edge (0) up to the room -y edge.
        r.x0 = ox + cx;
        r.x1 = ox + cx + kCorridorWidth - 1;
        r.y0 = 0;
        r.y1 = oy - 1;
        break;
    case DIR_POS_Y: // +y: room +y edge (oy+h) out to the block edge.
        r.x0 = ox + cx;
        r.x1 = ox + cx + kCorridorWidth - 1;
        r.y0 = oy + h;
        r.y1 = kBlock - 1;
        break;
    default:
        break; // empty rect
    }
    return r;
}

std::vector<int> FloorBlock::Build(const RoomGen &rg,
                                   const std::array<int, 4> &entrance) {
    std::vector<int> block(static_cast<std::size_t>(kBlock) *
                               static_cast<std::size_t>(kBlock),
                           kMarginCell);
    const auto set = [&](int x, int y, int v) {
        block[static_cast<std::size_t>(x) * static_cast<std::size_t>(kBlock) +
              static_cast<std::size_t>(y)] = v;
    };

    // 1) Stamp the room verbatim, centred at its offset.
    const int ox = OffsetX(rg);
    const int oy = OffsetY(rg);
    for (int rx = 0; rx < rg.Width(); ++rx) {
        for (int ry = 0; ry < rg.Height(); ++ry) {
            set(ox + rx, oy + ry, rg.At(rx, ry));
        }
    }

    // 2) Carve the corridor (walkable) toward each connected neighbour. The strip
    //    flanks stay margin (solid), so the corridor is walled on its long sides.
    for (int dir = 0; dir < 4; ++dir) {
        if (entrance[static_cast<std::size_t>(dir)] != 1) {
            continue;
        }
        const Rect s = CorridorStrip(dir, rg);
        for (int x = s.x0; x <= s.x1; ++x) {
            for (int y = s.y0; y <= s.y1; ++y) {
                if (x >= 0 && y >= 0 && x < kBlock && y < kBlock) {
                    set(x, y, kCorridorCell);
                }
            }
        }
    }
    return block;
}

std::vector<std::pair<int, int>>
FloorBlock::ConnectedFloorCells(const RoomGen &rg) {
    const int w = rg.Width();
    const int h = rg.Height();
    const auto idx = [h](int x, int y) {
        return static_cast<std::size_t>(x) * static_cast<std::size_t>(h) +
               static_cast<std::size_t>(y);
    };
    const auto walkable = [&](int x, int y) {
        const int c = rg.At(x, y);
        return c == 0 || c == -2 || c == 11; // floor / aisle / door
    };

    // Flood from every aisle(-2)/door(11) mouth through the walkable interior.
    std::vector<char> seen(static_cast<std::size_t>(w) *
                               static_cast<std::size_t>(h),
                           0);
    std::vector<std::pair<int, int>> stack;
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const int c = rg.At(x, y);
            if ((c == -2 || c == 11) && seen[idx(x, y)] == 0) {
                seen[idx(x, y)] = 1;
                stack.push_back({x, y});
            }
        }
    }
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    while (!stack.empty()) {
        const auto [cx, cy] = stack.back();
        stack.pop_back();
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + dx[d];
            const int ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                continue;
            }
            if (seen[idx(nx, ny)] != 0 || !walkable(nx, ny)) {
                continue;
            }
            seen[idx(nx, ny)] = 1;
            stack.push_back({nx, ny});
        }
    }

    // Collect, in x-major scan order, the FLOOR (code 0) cells in that component.
    std::vector<std::pair<int, int>> cells;
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            if (rg.At(x, y) == 0 && seen[idx(x, y)] != 0) {
                cells.emplace_back(x, y);
            }
        }
    }

    // Fallback: a room with no carved aisle (no neighbour) -> the full floor list.
    if (cells.empty()) {
        return rg.FloorList();
    }
    return cells;
}

std::vector<std::pair<int, int>> FloorBlock::DoorSealCells(const RoomGen &rg) {
    const int w = rg.Width();
    const int h = rg.Height();
    std::vector<std::pair<int, int>> cells;
    // The full door opening = every WALKABLE cell (aisle -2 or door 11) on the room
    // PERIMETER. Sealing the whole band (not just the two code-11 flanks) is what
    // actually contains the player + enemies while the room is locked.
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const bool perimeter =
                (x == 0 || x == w - 1 || y == 0 || y == h - 1);
            if (!perimeter) {
                continue;
            }
            const int code = rg.At(x, y);
            if (code == -2 || code == 11) {
                cells.emplace_back(x, y);
            }
        }
    }
    return cells;
}

} // namespace Game
