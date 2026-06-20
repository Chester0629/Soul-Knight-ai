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
            m_RoomWidth = SizeFromIndex(m_Rng.Range(0, 2));
        } else {
            m_RoomWidth =
                SizeFromIndex(m_Rng.Range(0, static_cast<int>(kRoomSizes.size())));
        }
        m_RoomHeight =
            SizeFromIndex(m_Rng.Range(0, static_cast<int>(kRoomSizes.size())));

        // Roll difficulty bands: baseLevel from size + Range(0,2) jitter / axis.
        const int baseLevel = ComputeBaseLevel(m_RoomWidth, m_RoomHeight);
        m_WallLevel = m_Rng.Range(0, 2) + baseLevel;
        m_ObstacleLevel = m_Rng.Range(0, 2) + baseLevel;
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

// FAITHFUL: RGRoomX__CreateFloor @ rva 0x4FD3E4
//
// Three passes over the room_width x room_height grid. The decomp draws RNG only
// in pass 1 (noise) and pass 3 (variant pick); the placed floor variant is a
// visual-only choice (it does NOT write back into map[]), so for the pure grid
// the observable effect is solely the RNG draws that advance the stream. We
// reproduce the EXACT draw count and order so downstream stages stay in sync.
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
            noise[nIdx(x, y)] = m_Rng.Range(0, 2);
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

    // Pass 3 - choose a floor variant from the classification. The decomp draws,
    // per cell, in this exact conditional order (recovered from the branch
    // structure; the chosen variant value is visual-only and never written back):
    //   1) if edge in [1,2]   -> Range(0,4)
    //   2) if edge >= 4       -> Range(0,4)
    //   3) ALWAYS             -> Range(2,4)
    // (1) and (2) are mutually exclusive, so each cell draws 1 or 2 values.
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const int cls = edge[nIdx(x, y)];
            if (cls >= 1 && cls <= 2) {
                (void)m_Rng.Range(0, 4);
            }
            if (cls >= 4) {
                (void)m_Rng.Range(0, 4);
            }
            (void)m_Rng.Range(2, 4);
        }
    }
}

// FAITHFUL: RGRoomX__CreateWall @ rva 0x4FDB6C
//
// Two perimeter loops. The decomp iterates WORLD coordinates and rolls one
// Range(0,100) decoration check per placed wall tile, skipping the door-gap
// window where the unsigned test `4 < (i - 0x12)` holds (i.e. world index in
// [18,22] is skipped). The roll outcome is visual only; we reproduce the draw
// count + skip window so the stream stays faithful.
void RoomGen::CreateWall() {
    // Horizontal walls: world x from room_x_offset-1 to room_x_offset+width.
    const int xStart = m_RoomXOffset - 1;
    const int xEnd = m_RoomXOffset + m_RoomWidth + 1;
    for (int x = xStart; x < xEnd; ++x) {
        // Skip the door gap: world x in [18, 22].
        if (x >= 18 && x <= 22) {
            continue;
        }
        (void)m_Rng.Range(0, 100);
    }

    // Vertical walls: world y from room_y_offset to room_y_offset+height.
    const int yStart = m_RoomYOffset;
    const int yEnd = m_RoomYOffset + m_RoomHeight;
    for (int y = yStart; y < yEnd; ++y) {
        if (y >= 18 && y <= 22) {
            continue;
        }
        (void)m_Rng.Range(0, 100);
    }
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
        bigCount = m_Rng.Range(10, 15);
        break;
    case 2:
        bigCount = m_Rng.Range(6, 10);
        break;
    case 1:
        bigCount = m_Rng.Range(3, 6);
        break;
    default:
        bigCount = 0;
        break;
    }
    for (int n = 0; n < bigCount; ++n) {
        const int roll = m_Rng.Range(0, 100);
        const int rw = m_Rng.Range(1, 5);
        const int rh = m_Rng.Range(1, 5);

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

        const int px = m_Rng.Range(0, m_RoomWidth - w);
        const int py = m_Rng.Range(0, m_RoomHeight - h);

        if (!IsWallIntersect(px, py, w, h)) {
            continue;
        }
        StampBigObstacle(px, py, w, h);
    }

    // ---- Phase B : small destructible obstacles (count from obstacle_level) -
    int smallCount = 0;
    switch (m_ObstacleLevel) {
    case 3:
        smallCount = m_Rng.Range(7, 10);
        break;
    case 2:
        smallCount = m_Rng.Range(4, 7);
        break;
    case 1:
        smallCount = m_Rng.Range(1, 4);
        break;
    default:
        smallCount = 0;
        break;
    }
    for (int n = 0; n < smallCount; ++n) {
        const int w = m_Rng.Range(1, 4);
        const int h = m_Rng.Range(1, 4);
        const int px = m_Rng.Range(0, m_RoomWidth - w);
        const int py = m_Rng.Range(0, m_RoomHeight - h);

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
                if (m_Rng.Range(0, 100) < m_SpecialBoxRate) {
                    (void)m_Rng.Range(2, 5);
                }
            } else if (code == 0) {
                m_FloorList.emplace_back(x, y);
            }
            // codes -1 (border), -2 (aisle), 1, 8, 9, 11, >2 add nothing here.
        }
    }
}

} // namespace Game
