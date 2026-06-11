#include "world/RGAisle.hpp"

namespace Game {

std::size_t RGAisle::Index(int x, int y, int height) {
    return static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
           static_cast<std::size_t>(y);
}

// FAITHFUL: RGAisle__CreateFloor @ game_full.c:390274-390289 (dim selector)
//
// The decomp tests (direction | 2) == 2, which holds for NORTH(0) and WEST(2):
//   uVar12 (height) = 5; uVar11 (width) = room[0x70] - 1  (room_x_offset - 1)
// otherwise (EAST(1)/SOUTH(3)):
//   uVar11 (width)  = 5; uVar12 (height) = room[0x74] - 1  (room_y_offset - 1)
RGAisle::Dims RGAisle::GridDims(int direction, int roomXOffset,
                                int roomYOffset) {
    Dims dims;
    if ((static_cast<unsigned int>(direction) | 2U) == 2U) {
        // NORTH / WEST: corridor runs along x out to the dungeon edge.
        dims.width = roomXOffset - 1;
        dims.height = kAisleShortExtent;
    } else {
        // EAST / SOUTH: corridor runs along y out to the dungeon edge.
        dims.width = kAisleShortExtent;
        dims.height = roomYOffset - 1;
    }
    return dims;
}

// FAITHFUL: RGAisle__CreateFloor @ game_full.c:390290-390424 (pass 1 + pass 2)
//
// Pass 1 fills a width x height noise grid with one Range(0,2) per cell. Pass 2
// classifies every cell by its empty (==0) orthogonal neighbours using the
// decomp's weighted up-neighbour counting (no RNG). Pass 3 (the variant pick +
// tile Instantiate) is owner-side and intentionally not run here; the stream is
// left positioned exactly at the end of pass 1's draws so the owner can resume.
std::vector<int> RGAisle::BuildFloorEdges(const Dims &dims) {
    const int w = dims.width;
    const int h = dims.height;

    // Degenerate corridor (offset-1 <= 0): the decomp's `0 < uVar11`/`0 < uVar12`
    // guards skip every loop, so no grid and no draws. Faithful early-out.
    if (w <= 0 || h <= 0) {
        return {};
    }

    const std::size_t cells =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    std::vector<int> noise(cells, 0);
    std::vector<int> edge(cells, 0);

    // ---- Pass 1: random base noise (one Range(0,2) per cell) ----------------
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            noise[Index(x, y, h)] = m_Rng.Range(0, kNoiseCeiling);
        }
    }

    // ---- Pass 2: classify by empty (==0) orthogonal neighbours (no RNG) -----
    // Faithful to the decomp's weighted up-neighbour counting:
    //   start count = (self==0) ? 1 : 0;
    //   left  neighbour: if empty, count := (self==0) ? 2 : 1 (overwrite);
    //   right/up/down neighbours: if empty, count += 1.
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const int self = noise[Index(x, y, h)];
            int count = (self == 0) ? 1 : 0;

            if (x > 0) {
                const int wgt = (self == 0) ? 2 : 1;
                if (noise[Index(x - 1, y, h)] == 0) {
                    count = wgt;
                }
            }
            if (x < w - 1) {
                if (noise[Index(x + 1, y, h)] == 0) {
                    ++count;
                }
            }
            if (y > 0) {
                if (noise[Index(x, y - 1, h)] == 0) {
                    ++count;
                }
            }
            if (y < h - 1) {
                if (noise[Index(x, y + 1, h)] == 0) {
                    ++count;
                }
            }
            edge[Index(x, y, h)] = count;
        }
    }

    // Pass 3 (variant pick + Instantiate) is owner-side; see
    // RGAisle::FloorDrawsAreOwnerSide. We stop here, leaving the stream exactly
    // where pass 1 left it (pass 2 takes no draw).
    return edge;
}

// FAITHFUL: RGAisle__CreateAisleWall @ game_full.c:390523
//
// Direction selector (direction | 2) == 2 picks the active axis offset:
//   NORTH/WEST -> room[0x70]-1 (room_x_offset-1); EAST/SOUTH -> room[0x74]-1.
// Gated on `0 < offset-1`: when the corridor has length, draw Range(0,100)
// twice. The first roll selects a 0/1 wall-tile prefab index (`iVar1 < 6`); the
// second roll is consumed but its value is otherwise unused (it only advances
// the stream). When the gate fails, NO draw is taken.
RGAisle::WallRoll RGAisle::RollAisleWall(int direction, int roomXOffset,
                                         int roomYOffset) {
    WallRoll out;

    int aisleLength = 0;
    if ((static_cast<unsigned int>(direction) | 2U) == 2U) {
        aisleLength = roomXOffset - 1;
    } else {
        aisleLength = roomYOffset - 1;
    }

    if (aisleLength <= 0) {
        // Gate fails: faithful zero-draw early-out (placed stays false).
        return out;
    }

    out.placed = true;
    out.firstRoll = m_Rng.Range(0, kWallRollCeiling);
    out.secondRoll = m_Rng.Range(0, kWallRollCeiling);
    out.prefabIndex = (out.firstRoll < kWallVariantThreshold) ? 1 : 0;
    return out;
}

} // namespace Game
