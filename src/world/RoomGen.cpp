#include "world/RoomGen.hpp"

#include <cstddef>

namespace Game {

namespace {

// Difficulty base level derived from room size. The decomp compares the rolled
// size against thresholds 0x10 (16) and 0x16 (22) and reads DAT_0050F930/0F934
// float constants; this reproduces the documented branch structure. The exact
// float weights live in unresolved static data, so the {1, 1.5, 2} / {+0.5, +1}
// banding is the best-supported reading (see manual flags).
// FAITHFUL: RGRoomX__SetRGRandomSeed @ rva 0x4FF350 (size->level tail)
int ComputeBaseLevel(int w, int h) {
    float lvl = 1.0F;
    if (w < 16) {
        lvl = 1.0F;
    } else {
        lvl = (w < 22) ? 1.5F : 2.0F;
    }
    if (h >= 16) {
        lvl += (h < 22) ? 0.5F : 1.0F;
    }
    return static_cast<int>(lvl);
}

// Map a banded size-table index onto kRoomSizes with clamping.
int SizeFromIndex(int i) {
    if (i < 0) {
        i = 0;
    }
    if (i >= static_cast<int>(RoomGen::kRoomSizes.size())) {
        i = static_cast<int>(RoomGen::kRoomSizes.size()) - 1;
    }
    return RoomGen::kRoomSizes[static_cast<std::size_t>(i)];
}

} // namespace

// FAITHFUL: RGRoomX__SetRGRandomSeed @ rva 0x4FF350
RoomGen::RoomGen(int seed, const std::array<int, 4> &entrance,
                 const Options &options)
    : m_Entrance(entrance) {
    // 1) Seed the deterministic RNG for this room.
    m_Rng.SetRandomSeed(seed);

    // Phase 3 (design-room interior): capture the design-obstacle inputs. Default
    // (proceduralObstacles=true, empty designSolidCells) is byte-identical to the
    // pre-Phase-3 build, so every golden/procedural path is unchanged.
    m_LegacyDrawOrder = options.legacyDrawOrder;
    m_ProceduralObstacles = options.proceduralObstacles;
    m_DesignSolidCells = options.designSolidCells;

    // 2) Badass mode bumps the special-box drop rate (before any size rolls).
    if (options.badassMode) {
        m_SpecialBoxRate = 20;
    }

    if (options.randomRoom) {
        // 3) Roll room_width / room_height from the banded size table {15,21,25}.
        //    floorIndex%5==1 narrows the WIDTH roll to the first two entries;
        //    the height roll always uses the full table.
        if (options.floorIndex % 5 == 1) {
            m_RoomWidth = SizeFromIndex(Roll(0, 2));
        } else {
            m_RoomWidth =
                SizeFromIndex(Roll(0, static_cast<int>(kRoomSizes.size())));
        }
        m_RoomHeight =
            SizeFromIndex(Roll(0, static_cast<int>(kRoomSizes.size())));

        // Roll difficulty bands: baseLevel from size + Range(0,2) jitter / axis.
        const int baseLevel = ComputeBaseLevel(m_RoomWidth, m_RoomHeight);
        m_WallLevel = Roll(0, 2) + baseLevel;
        m_ObstacleLevel = Roll(0, 2) + baseLevel;
    } else {
        m_RoomWidth = options.roomWidth;
        m_RoomHeight = options.roomHeight;
        m_WallLevel = options.wallLevel;
        m_ObstacleLevel = options.obstacleLevel;
    }

    // 4) Centre the room inside the fixed 41x41 logical grid.
    m_RoomXOffset = (MAP_SIZE - m_RoomWidth) / 2;
    m_RoomYOffset = (MAP_SIZE - m_RoomHeight) / 2;

    // 5) Allocate the build grid (faithful int[room_width, room_height], all 0).
    m_Map.assign(static_cast<std::size_t>(m_RoomWidth) *
                     static_cast<std::size_t>(m_RoomHeight),
                 0);

    // 7) Build the room (procedural path). Fixed-room LoadRoom is data-driven
    //    and out of scope for this pure grid generator.
    SetUpRoom();
}

int RoomGen::Get(int x, int y) const {
    const std::size_t idx =
        static_cast<std::size_t>(x) * static_cast<std::size_t>(m_RoomHeight) +
        static_cast<std::size_t>(y);
    return m_Map[idx];
}

void RoomGen::Set(int x, int y, int value) {
    const std::size_t idx =
        static_cast<std::size_t>(x) * static_cast<std::size_t>(m_RoomHeight) +
        static_cast<std::size_t>(y);
    m_Map[idx] = value;
}

int RoomGen::At(int x, int y) const {
    if (x < 0 || y < 0 || x >= m_RoomWidth || y >= m_RoomHeight) {
        return 0;
    }
    return Get(x, y);
}

// FAITHFUL: RGRoomX__SetUpRoom @ rva 0x4FE234
void RoomGen::SetUpRoom() {
    // Stamp the room perimeter into map[] as border (-1).
    for (int x = 0; x < m_RoomWidth; ++x) {
        Set(x, 0, -1);
        Set(x, m_RoomHeight - 1, -1);
    }
    for (int y = 0; y < m_RoomHeight; ++y) {
        Set(0, y, -1);
        Set(m_RoomWidth - 1, y, -1);
    }

    // Generation pipeline - this exact order fixes the RNG sequence.
    CreateAisle();    // no grid RNG (corridor visuals are scene-side)
    CreateFloor();    // pass1/pass3 RNG
    CreateWall();     // perimeter decoration RNG
    CreateObstacle(); // big/small/render RNG
}

// FAITHFUL: RGRoomX__CreateAisle @ rva 0x4FCB74
//
// Carves the four optional corridors and door cells. Coordinates are taken
// verbatim from the decompiler's index math (map[x,y] = x*room_height + y):
//   EAST  (entrance[0]): cols rw-1 & rw-2, rows cy..cy+4 = -2;
//                        doors (rw-1, cy-1) and (rw-1, cy+5).
//   NORTH (entrance[1]): rows 0 & 1, cols cx..cx+4 = -2;
//                        doors (cx-1, 0) and (cx+5, 0).
//   WEST  (entrance[2]): col 0 (offset+0) & col... = -2 along the rows cy..cy+4;
//                        doors (cx... ) - see below.
//   SOUTH (entrance[3]): rows rh-1 & rh-2, cols cx..cx+4 = -2;
//                        doors (cx-1, rh-1) and (cx+5, rh-1).
// where cx = (room_width-5)/2, cy = (room_height-5)/2.
void RoomGen::CreateAisle() {
    const int cx = (m_RoomWidth - 5) / 2;
    const int cy = (m_RoomHeight - 5) / 2;

    // ---- EAST door (entrance[0]) --------------------------------------------
    // outer col rw-1, inner col rw-2; doors at (rw-1, cy-1) and (rw-1, cy+5).
    if (m_Entrance[0] == 1) {
        for (int i = 0; i <= 4; ++i) {
            Set(m_RoomWidth - 1, cy + i, -2);
            Set(m_RoomWidth - 2, cy + i, -2);
        }
        Set(m_RoomWidth - 1, cy - 1, 11);
        Set(m_RoomWidth - 1, cy + 5, 11);
    }

    // ---- NORTH door (entrance[1]) -------------------------------------------
    // The decomp writes map[cx+i, 0] (offset +0x10) and map[cx+i, 1] (offset
    // +0x14 = next y word); doors at (cx-1, 0) and (cx+5, 0).
    if (m_Entrance[1] == 1) {
        for (int i = 0; i <= 4; ++i) {
            Set(cx + i, 0, -2);
            Set(cx + i, 1, -2);
        }
        Set(cx - 1, 0, 11);
        Set(cx + 5, 0, 11);
    }

    // ---- WEST door (entrance[2]) --------------------------------------------
    // The decomp west branch indexes map at linear (cy+i) for the outer write
    // (column 0, i.e. map[0, cy+i]) and (cy+i + room_height) for the inner
    // (column 1, map[1, cy+i]); doors at (0, cy-1) and (0, cy+5).
    if (m_Entrance[2] == 1) {
        for (int i = 0; i <= 4; ++i) {
            Set(0, cy + i, -2);
            Set(1, cy + i, -2);
        }
        Set(0, cy - 1, 11);
        Set(0, cy + 5, 11);
    }

    // ---- SOUTH door (entrance[3]) -------------------------------------------
    // rows rh-1 (outer) & rh-2 (inner), cols cx..cx+4; doors (cx-1, rh-1) and
    // (cx+5, rh-1).
    if (m_Entrance[3] == 1) {
        for (int i = 0; i <= 4; ++i) {
            Set(cx + i, m_RoomHeight - 1, -2);
            Set(cx + i, m_RoomHeight - 2, -2);
        }
        Set(cx - 1, m_RoomHeight - 1, 11);
        Set(cx + 5, m_RoomHeight - 1, 11);
    }
}

// Every draw goes through here so the per-room draw count is observable (RoomGenTest pins the
// per-stage counts of the original: W*H noise + W*H variant + 2 per non-gap perimeter position).
int RoomGen::Roll(int minInclusive, int maxExclusive) {
    ++m_Draws;
    return m_Rng.Range(minInclusive, maxExclusive);
}

// FAITHFUL: RGRoomX$$CreateFloor pass 3 (DISASM 0x50d8d0-0x50d9dc), see CreateFloor.
int RoomGen::FloorVariantFromEdge(int edge) {
    if (edge >= 1 && edge <= 2) {
        return Roll(0, 4) > 2 ? 1 : 0;
    }
    if (edge <= 3) { // edge 0 or 3
        return Roll(2, 4);
    }
    return Roll(0, 4) < 3 ? 5 : 4; // edge >= 4
}

// FAITHFUL: RGRoomX__CreateFloor @ rva 0x4FD3E4 (DISASM 0x50d3e4-0x50db6c)
//
// Three passes over the room_width x room_height grid, x outer / y inner:
//   pass 1 (0x50d4b4): one Range(0,2) per cell -> noise
//   pass 2 (0x50d544-0x50d83c): NO draw; classify each cell by its empty (==0) neighbours -> edge
//   pass 3 (0x50d8d0-0x50d9dc): ONE draw per cell, chosen by edge -> floor_list index (see below)
// The variant does not write back into map[]; the orchestrator draws floor_list[idx] at the cell.
void RoomGen::CreateFloor() {
    const int w = m_RoomWidth;
    const int h = m_RoomHeight;

    std::vector<int> noise(static_cast<std::size_t>(w) *
                           static_cast<std::size_t>(h));
    std::vector<int> edge(static_cast<std::size_t>(w) *
                          static_cast<std::size_t>(h));

    auto nIdx = [h](int x, int y) {
        return static_cast<std::size_t>(x) * static_cast<std::size_t>(h) +
               static_cast<std::size_t>(y);
    };

    // Pass 1 - random base noise (one Range(0,2) per cell).
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            noise[nIdx(x, y)] = Roll(0, 2);
        }
    }

    // Pass 2 - classify each cell by its empty (==0) orthogonal neighbours.
    // (No RNG.) Faithful to the decomp's weighted up-neighbour counting.
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const int self = noise[nIdx(x, y)];
            int count = (self == 0) ? 1 : 0;

            if (x > 0) {
                const int wgt = (self == 0) ? 2 : 1;
                if (noise[nIdx(x - 1, y)] == 0) {
                    count = wgt;
                }
            }
            if (x < w - 1) {
                if (noise[nIdx(x + 1, y)] == 0) {
                    ++count;
                }
            }
            if (y > 0) {
                if (noise[nIdx(x, y - 1)] == 0) {
                    ++count;
                }
            }
            if (y < h - 1) {
                if (noise[nIdx(x, y + 1)] == 0) {
                    ++count;
                }
            }
            edge[nIdx(x, y)] = count;
        }
    }

    // Pass 3 - choose the floor variant. FAITHFUL: RGRoomX$$CreateFloor DISASM 0x50d8d0-0x50d9dc
    // (tools/arm_disasm.py 0x50d3e4 0x50db6c). Exactly ONE draw per cell, in the same x-outer /
    // y-inner order, chosen by the pass-2 classification `edge` (= the count the pass-2 loop wrote
    // to its second array):
    //   edge in [1,2] : cmp #1/#2 -> Range(0,4); idx = (roll > 2) ? 1 : 0     (0xd9d4-0xd9dc)
    //   edge 0 or 3   : cmp #3 ble -> Range(2,4); idx = roll (2 or 3)          (0xd9a0-0xd9ac)
    //   edge >= 4     : Range(0,4); idx = (roll < 3) ? 5 : 4                   (0xd984-0xd998)
    // idx is then the MapManager.floor_list index (ldr [MapManager+0x5c] list, 0xda0c) the cell is
    // Instantiated from, so the six variants f501..f506 all occur (the old port kept the raw 0..3
    // and added an extra always-draw Range(2,4), which desynced every later stage).
    m_FloorVariant.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            if (m_LegacyDrawOrder) {
                // pre-P2.20 stream (see Options::legacyDrawOrder): classification draw + always-draw
                const int cls = edge[nIdx(x, y)];
                if ((cls >= 1 && cls <= 2) || cls >= 4) {
                    (void)Roll(0, 4);
                }
                m_FloorVariant[nIdx(x, y)] = Roll(2, 4);
                continue;
            }
            m_FloorVariant[nIdx(x, y)] = FloorVariantFromEdge(edge[nIdx(x, y)]);
        }
    }
}

int RoomGen::FloorVariantAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= m_RoomWidth || y >= m_RoomHeight) {
        return 0;
    }
    const std::size_t i = static_cast<std::size_t>(x) * static_cast<std::size_t>(m_RoomHeight) +
                          static_cast<std::size_t>(y);
    return i < m_FloorVariant.size() ? m_FloorVariant[i] : 0;
}

// FAITHFUL: RGRoomX__CreateWall @ rva 0x4FDB6C, DISASM 0x50db6c-0x50e20c
// (python tools/arm_disasm.py 0x50db6c 0x50e210).
//
// Two perimeter loops over WORLD coordinates; the door-gap window is skipped with the unsigned
// test `4 < (i - 0x12)` (0x50dc00 `sub r2,r8,#0x12; cmp r2,#5; blo`), i.e. world index in
// [18,22] places nothing and draws nothing. Every other position draws TWO Range(0,100) values
// back to back (0x50dc24 / 0x50dc54 for x; 0x50df58 / 0x50df88 for y); each is compared with
// `cmp r0,#0x1e; movwlt rN,#1`, i.e. roll < 30 -> MapManager.wall_list[1] (the decorated wall,
// e.g. wall504), else wall_list[0]:
//   x loop, x = room_x_offset-1 .. room_x_offset+room_width (inclusive, corners included):
//     roll A -> piece at (x, room_y_offset-1)             (south row)
//     roll B -> piece at (x, room_y_offset+room_height)    (north row)
//   y loop, y = room_y_offset .. room_y_offset+room_height-1:
//     roll A -> piece at (room_x_offset-1, y)              (west column)
//     roll B -> piece at (room_x_offset+room_width, y)     (east column)
// (the piece positions are the Vector3 constructions + Transform.set_localPosition after each pair of rolls:
// 0x50de24-0x50de48 (x, yoff-1), 0x50de9c-0x50dec0 (x, yoff+h), 0x50e150-0x50e174 (xoff-1, y),
// 0x50e1c8-0x50e1e8 (xoff+w, y)).
void RoomGen::CreateWall() {
    const int xStart = m_RoomXOffset - 1;
    const int xEnd = m_RoomXOffset + m_RoomWidth + 1; // exclusive
    const int yStart = m_RoomYOffset;
    const int yEnd = m_RoomYOffset + m_RoomHeight; // exclusive
    m_WallBaseX = xStart;
    m_WallBaseY = yStart;
    for (auto &side : m_WallDeco) {
        side.clear();
    }
    m_WallDeco[static_cast<std::size_t>(WallSide::South)].assign(
        static_cast<std::size_t>(xEnd - xStart), kWallNoRoll);
    m_WallDeco[static_cast<std::size_t>(WallSide::North)].assign(
        static_cast<std::size_t>(xEnd - xStart), kWallNoRoll);
    m_WallDeco[static_cast<std::size_t>(WallSide::West)].assign(
        static_cast<std::size_t>(yEnd - yStart), kWallNoRoll);
    m_WallDeco[static_cast<std::size_t>(WallSide::East)].assign(
        static_cast<std::size_t>(yEnd - yStart), kWallNoRoll);

    if (m_LegacyDrawOrder) {
        // pre-P2.20 stream (see Options::legacyDrawOrder): ONE discarded roll per non-gap position
        for (int x = xStart; x < xEnd; ++x) {
            if (x < kGapLo || x > kGapHi) {
                (void)Roll(0, 100);
            }
        }
        for (int y = yStart; y < yEnd; ++y) {
            if (y < kGapLo || y > kGapHi) {
                (void)Roll(0, 100);
            }
        }
        return;
    }
    for (int x = xStart; x < xEnd; ++x) {
        if (x >= kGapLo && x <= kGapHi) {
            continue; // door-gap window: no piece, no draw
        }
        const std::size_t i = static_cast<std::size_t>(x - xStart);
        m_WallDeco[static_cast<std::size_t>(WallSide::South)][i] = Roll(0, 100) < kWallDecoThreshold ? 1 : 0;
        m_WallDeco[static_cast<std::size_t>(WallSide::North)][i] = Roll(0, 100) < kWallDecoThreshold ? 1 : 0;
    }
    for (int y = yStart; y < yEnd; ++y) {
        if (y >= kGapLo && y <= kGapHi) {
            continue;
        }
        const std::size_t i = static_cast<std::size_t>(y - yStart);
        m_WallDeco[static_cast<std::size_t>(WallSide::West)][i] = Roll(0, 100) < kWallDecoThreshold ? 1 : 0;
        m_WallDeco[static_cast<std::size_t>(WallSide::East)][i] = Roll(0, 100) < kWallDecoThreshold ? 1 : 0;
    }
}

bool RoomGen::WallPieceDecorated(WallSide side, int along) const {
    const auto &v = m_WallDeco[static_cast<std::size_t>(side)];
    const bool horizontal = side == WallSide::South || side == WallSide::North;
    const int idx = along - (horizontal ? m_WallBaseX : m_WallBaseY);
    if (idx < 0 || static_cast<std::size_t>(idx) >= v.size()) {
        return false;
    }
    return v[static_cast<std::size_t>(idx)] == 1;
}

bool RoomGen::WallPieceRolled(WallSide side, int along) const {
    const auto &v = m_WallDeco[static_cast<std::size_t>(side)];
    const bool horizontal = side == WallSide::South || side == WallSide::North;
    const int idx = along - (horizontal ? m_WallBaseX : m_WallBaseY);
    if (idx < 0 || static_cast<std::size_t>(idx) >= v.size()) {
        return false;
    }
    return v[static_cast<std::size_t>(idx)] != kWallNoRoll;
}

// PORT CHOICE (T-73): the port draws the wall ring INSIDE the room grid (the border cells,
// code -1), while the original's ring stands one cell OUTSIDE it. A border cell therefore wears
// the roll of the original piece in the same world column / row: south row y=0 and north row
// y=H-1 use the x-loop rolls (corners included), the remaining west / east columns use the y-loop
// rolls; the x-loop's two extra corner pieces (x = xoff-1, xoff+W) have no port cell.
bool RoomGen::BorderCellDecorated(int x, int y) const {
    if (x < 0 || y < 0 || x >= m_RoomWidth || y >= m_RoomHeight) {
        return false;
    }
    if (y == 0) {
        return WallPieceDecorated(WallSide::South, m_RoomXOffset + x);
    }
    if (y == m_RoomHeight - 1) {
        return WallPieceDecorated(WallSide::North, m_RoomXOffset + x);
    }
    if (x == 0) {
        return WallPieceDecorated(WallSide::West, m_RoomYOffset + y);
    }
    if (x == m_RoomWidth - 1) {
        return WallPieceDecorated(WallSide::East, m_RoomYOffset + y);
    }
    return false;
}

// FAITHFUL: RGRoomX__IsWallIntersect @ rva 0x4FF158
//
// Returns true (free) iff every cell in [rx-1, rx+rw+1] x [ry-1, ry+rh+1] that
// lies inside the room bounds is empty (map==0). Out-of-bounds cells are ignored.
bool RoomGen::IsWallIntersect(int rx, int ry, int rw, int rh) const {
    bool free = true;
    for (int x = rx - 1; x <= rx + rw + 1; ++x) {
        for (int y = ry - 1; y <= ry + rh + 1; ++y) {
            if (x < 0 || y < 0) {
                continue;
            }
            if (x >= m_RoomWidth || y >= m_RoomHeight) {
                continue;
            }
            free = free && (Get(x, y) == 0);
        }
    }
    return free;
}

// Stamp a big-obstacle footprint with the decomp codes: surrounding ring -> 1,
// interior -> -1, 1x1 -> 8, 2x2 centre -> 9.
void RoomGen::StampBigObstacle(int px, int py, int w, int h) {
    for (int x = px - 1; x < px + w + 2; ++x) {
        for (int y = py - 1; y < py + h + 2; ++y) {
            if (x < 0 || y < 0 || x >= m_RoomWidth || y >= m_RoomHeight) {
                continue;
            }
            const bool insideX = (x >= px && x <= px + w - 1);
            const bool insideY = (y >= py && y <= py + h - 1);
            if (!insideX || !insideY) {
                Set(x, y, 1); // surrounding ring -> solid obstacle
            } else {
                if (w == 2 && h == 2) {
                    Set(px + 1, py, 9); // 2x2 centre marker
                } else if (w == 1 && h == 1) {
                    Set(px, py, 8); // 1x1 marker
                } else {
                    Set(x, y, -1); // larger interior -> filled border
                }
            }
        }
    }
}

// FAITHFUL: RGRoomX__CreateObstacle @ rva 0x4FE424
//
// Phase A: big obstacles, count from wall_level. Phase B: small destructibles,
// count from obstacle_level. Render pass: walk the grid x-major and append every
// open (==0) cell to floor_list (as room-local (x,y)); destructible cells roll a
// special-box check; decorative codes are visual. The count bands and the
// render-pass draws are confirmed verbatim in the decompilation; the placement
// loops (truncated by the decompiler's count-call bailout) follow the
// cross-checked reference reconstruction.
void RoomGen::CreateObstacle() {
    // Phase 3 design mode (proceduralObstacles==false): SKIP the procedural
    // Phase A/B placement and instead stamp the prefab's wall markers
    // (obj_index==0 -> code 1) onto OPEN floor cells only. Never overwrite a
    // door (11), aisle (-2), or border (-1), so the RoomGen shell + the 5-wide
    // door band the corridor seam depends on stay intact. Boxes/traps/pads are
    // deliberately NOT stamped into the grid: they are a collision/trigger
    // overlay the orchestrator builds, so a box blocks movement (BlocksAny) but
    // never breaks DOOR REACHABILITY -- ConnectedFloorCells reads this grid and
    // only obj_index-0 walls are connectivity barriers (decision #2 = b',
    // Step 0.5 #2). Design stamping is zero-RNG (the shell stages already drew).
    if (!m_ProceduralObstacles) {
        for (const auto &c : m_DesignSolidCells) {
            const int x = c.first;
            const int y = c.second;
            if (x >= 0 && x < m_RoomWidth && y >= 0 && y < m_RoomHeight &&
                Get(x, y) == 0) {
                Set(x, y, 1);
            }
        }
        // Render pass below (x-major) still builds floor_list from the remaining
        // open cells -- spawn/connectivity see the design walls as barriers.
        for (int x = 0; x < m_RoomWidth; ++x) {
            for (int y = 0; y < m_RoomHeight; ++y) {
                if (Get(x, y) == 0) {
                    m_FloorList.emplace_back(x, y);
                }
            }
        }
        return;
    }

    // ---- Phase A : big obstacles (count driven by wall_level) ---------------
    int bigCount = 0;
    switch (m_WallLevel) {
    case 3:
        bigCount = Roll(10, 15);
        break;
    case 2:
        bigCount = Roll(6, 10);
        break;
    case 1:
        bigCount = Roll(3, 6);
        break;
    default:
        bigCount = 0;
        break;
    }
    for (int n = 0; n < bigCount; ++n) {
        const int roll = Roll(0, 100);
        const int rw = Roll(1, 5);
        const int rh = Roll(1, 5);

        int w = 1;
        int h = 1;
        if (roll >= 30) {
            if (roll < 60) {
                w = rw;
                h = rh;
            } else {
                w = 2;
                h = 2;
            }
        }

        const int px = Roll(0, m_RoomWidth - w);
        const int py = Roll(0, m_RoomHeight - h);

        if (!IsWallIntersect(px, py, w, h)) {
            continue;
        }
        StampBigObstacle(px, py, w, h);
    }

    // ---- Phase B : small destructible obstacles (count from obstacle_level) -
    int smallCount = 0;
    switch (m_ObstacleLevel) {
    case 3:
        smallCount = Roll(7, 10);
        break;
    case 2:
        smallCount = Roll(4, 7);
        break;
    case 1:
        smallCount = Roll(1, 4);
        break;
    default:
        smallCount = 0;
        break;
    }
    for (int n = 0; n < smallCount; ++n) {
        const int w = Roll(1, 4);
        const int h = Roll(1, 4);
        const int px = Roll(0, m_RoomWidth - w);
        const int py = Roll(0, m_RoomHeight - h);

        for (int x = px; x < px + w; ++x) {
            for (int y = py; y < py + h; ++y) {
                const int code = Get(x, y);
                if (code == 0 || code == -1) {
                    Set(x, y, 2);
                }
            }
        }
    }

    // ---- Render pass (x-major, y-minor) -------------------------------------
    // code==1 / code>2 : decorative/solid (no RNG, no grid change).
    // code==2          : roll Range(0,100); if < special_box_rate, pick a
    //                    treasure variant via Range(2,5) (advances the stream).
    // code==0          : append (x, y) to floor_list.
    for (int x = 0; x < m_RoomWidth; ++x) {
        for (int y = 0; y < m_RoomHeight; ++y) {
            const int code = Get(x, y);
            if (code == 2) {
                if (Roll(0, 100) < m_SpecialBoxRate) {
                    (void)Roll(2, 5);
                }
            } else if (code == 0) {
                m_FloorList.emplace_back(x, y);
            }
            // codes -1 (border), -2 (aisle), 1, 8, 9, 11, >2 add nothing here.
        }
    }
}

} // namespace Game
