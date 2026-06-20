#include "world/MapManager.hpp"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace Game {

namespace {
// Direction convention of the original walk: 0=up(+y) 1=down(-y) 2=left(-x)
// 3=right(+x). dx/dy index by that code.
constexpr int kDx[4] = {0, 0, -1, 1};
constexpr int kDy[4] = {1, -1, 0, 0};
constexpr int kWalkGuard = 1000; ///< matches the original's guard cap.
// Dedicated additive offset for the design-room selection stream, so it never
// shares draws with the random walk's m_Rng (which is seeded from `seed`).
constexpr int kDesignSeedOffset = 104729;
} // namespace

// FAITHFUL: MapManager.CreateMap @ rva 0x1171FFC (random-walk room placement).
MapManager::MapManager(int seed, const Options &options) {
    m_Rng.SetRandomSeed(seed);

    const int mapLong = std::max(1, options.mapLong);
    m_GridSize = options.gridSize > 0 ? options.gridSize
                                      : std::max(8, mapLong * 2 + 1);
    m_Relation.assign(static_cast<std::size_t>(m_GridSize) * m_GridSize, 0);

    const int cx = m_GridSize / 2;
    const int cy = m_GridSize / 2;

    // Start room at the grid centre (type 1).
    m_Relation[static_cast<std::size_t>(Index(cx, cy))] = 1;
    m_Rooms.push_back(RoomCell{cx, cy, 1, {0, 0, 0, 0}});

    bool hasSpecial = false;
    bool hasBadass = false;
    int guard = 0;
    while (static_cast<int>(m_Rooms.size()) < mapLong && guard < kWalkGuard) {
        ++guard;

        // Branch from a random already-placed room in a random direction.
        const int fromIndex = m_Rng.Range(0, static_cast<int>(m_Rooms.size()));
        const RoomCell &from = m_Rooms[static_cast<std::size_t>(fromIndex)];
        const int dir = m_Rng.Range(0, 4);
        const int nx = from.gridX + kDx[dir];
        const int ny = from.gridY + kDy[dir];

        if (nx < 0 || ny < 0 || nx >= m_GridSize || ny >= m_GridSize) {
            continue; // off-grid
        }
        if (HasRoom(nx, ny)) {
            continue; // IsInMapList: cell already occupied
        }

        // Room type: mostly normal (stored as 1); at most one special and one
        // badass per floor, each gated by ranRoomProbability.
        int roomType = 0;
        if (!hasSpecial && m_Rng.Range(0, 100) < options.ranRoomProbability) {
            roomType = 2;
            hasSpecial = true;
        } else if (!hasBadass &&
                   m_Rng.Range(0, 100) < options.ranRoomProbability) {
            roomType = 3;
            hasBadass = true;
        }
        const int stored = roomType == 0 ? 1 : roomType;
        m_Relation[static_cast<std::size_t>(Index(nx, ny))] = stored;
        m_Rooms.push_back(RoomCell{nx, ny, stored, {0, 0, 0, 0}});
    }

    // Derive each room's door flags from its placed orthogonal neighbours. The
    // index->edge mapping is FIXED by RoomGen::CreateAisle's carve sides (not by a
    // compass label): [0] carves max-x, [2] min-x, [1] min-y (cell y=0), [3] max-y
    // (cell y=h-1). Through CellToWorld + GameScene's origin, min-y/max-y are the
    // -y/+y world edges, so each door must be flagged for the neighbour on THAT
    // side: [1]<-(gridY-1), [3]<-(gridY+1). (The y pair was previously swapped to
    // the +y/-y neighbours, so vertical seams became solid double-walls and the
    // doors faced the outer void -- the room-to-room navigation bug.)
    for (RoomCell &room : m_Rooms) {
        room.entrance[0] = HasRoom(room.gridX + 1, room.gridY) ? 1 : 0; // max-x -> +x
        room.entrance[1] = HasRoom(room.gridX, room.gridY - 1) ? 1 : 0; // min-y -> -y
        room.entrance[2] = HasRoom(room.gridX - 1, room.gridY) ? 1 : 0; // min-x -> -x
        room.entrance[3] = HasRoom(room.gridX, room.gridY + 1) ? 1 : 0; // max-y -> +y
    }

    // --- Phase 2: assign a design room (id + size) per slot. Runs on a SEPARATE
    //     RNG stream (seed + kDesignSeedOffset), so the walk above -- and thus the
    //     map's determinism + every downstream seed -- is byte-identical with or
    //     without a design pool. No-op when no pool is supplied.
    if (!options.designRooms.empty()) {
        std::vector<int> slotTypes;
        slotTypes.reserve(m_Rooms.size());
        for (const RoomCell &room : m_Rooms) {
            slotTypes.push_back(room.type);
        }
        const std::vector<int> picks = SelectDesignRooms(
            options.designRooms, slotTypes, seed + kDesignSeedOffset);
        for (std::size_t i = 0; i < m_Rooms.size() && i < picks.size(); ++i) {
            const DesignRoom &dr =
                options.designRooms[static_cast<std::size_t>(picks[i])];
            m_Rooms[i].roomId = dr.id;
            m_Rooms[i].width = dr.width;
            m_Rooms[i].height = dr.height;
        }
    }
}

// FAITHFUL-RECONSTRUCTION (TODO[verify]): MapManager.SelectPool + GetRandomRoomIndex
// (RVA 0x1173B94, not byte-recoverable). Per slot, draw from the slot type's sub-pool
// (falling back to the type-1 pool when that type has no rooms), re-rolling while the
// chosen room was recently used (usedRoom by identity), up to the sub-pool size.
std::vector<int>
MapManager::SelectDesignRooms(const std::vector<DesignRoom> &pool,
                             const std::vector<int> &slotTypes, int seed) {
    std::vector<int> out;
    if (pool.empty()) {
        return out;
    }
    // Group room indices by type; keep the type-1 list as the fallback pool.
    std::unordered_map<int, std::vector<int>> byType;
    std::vector<int> type1;
    for (int i = 0; i < static_cast<int>(pool.size()); ++i) {
        byType[pool[static_cast<std::size_t>(i)].type].push_back(i);
        if (pool[static_cast<std::size_t>(i)].type == 1) {
            type1.push_back(i);
        }
    }
    // If there is no type-1 pool at all, fall back to the whole pool (defensive).
    std::vector<int> everything;
    if (type1.empty()) {
        for (int i = 0; i < static_cast<int>(pool.size()); ++i) {
            everything.push_back(i);
        }
    }
    const std::vector<int> &fallback = type1.empty() ? everything : type1;

    RGRandom rng;
    rng.SetRandomSeed(seed);
    std::vector<char> used(pool.size(), 0);
    out.reserve(slotTypes.size());
    for (int t : slotTypes) {
        const auto it = byType.find(t);
        // TODO[debt: special-room content] -- when this slot's type has no sub-pool
        // (floor-1 ships ONLY type-1 design rooms), a type-2 (special) / type-3
        // (badass) slot draws a TYPE-1 room SHAPE from `fallback`. So special/badass
        // rooms currently wear normal-room geometry: their unique floor-1 shapes are
        // MISSING and need a dedicated special-room RE pass (deferred). The port's
        // loot overlay (wallLevel + chest tier) on those slots stays port-as-is and is
        // UNVERIFIED for faithfulness pending that RE -- do NOT treat it as verified.
        // This is a CONTENT gap, distinct from the algorithm-fidelity TODO[verify]
        // above (SelectPool/GetRandomRoomIndex byte-recovery). See LEVEL_GEN_PLAN.md s7.
        const std::vector<int> &cands =
            (it != byType.end() && !it->second.empty()) ? it->second : fallback;
        const int n = static_cast<int>(cands.size());
        int pick = cands[static_cast<std::size_t>(rng.Range(0, n))];
        int spins = 0;
        while (used[static_cast<std::size_t>(pick)] != 0 && spins < n) {
            pick = cands[static_cast<std::size_t>(rng.Range(0, n))];
            ++spins;
        }
        used[static_cast<std::size_t>(pick)] = 1;
        out.push_back(pick);
    }
    return out;
}

bool MapManager::HasRoom(int gx, int gy) const {
    if (gx < 0 || gy < 0 || gx >= m_GridSize || gy >= m_GridSize) {
        return false;
    }
    return m_Relation[static_cast<std::size_t>(Index(gx, gy))] != 0;
}

int MapManager::RoomType(int gx, int gy) const {
    if (gx < 0 || gy < 0 || gx >= m_GridSize || gy >= m_GridSize) {
        return 0;
    }
    return m_Relation[static_cast<std::size_t>(Index(gx, gy))];
}

bool MapManager::Connected() const {
    if (m_Rooms.empty()) {
        return true;
    }
    // BFS over orthogonal room adjacency from the start room.
    std::vector<char> visited(
        static_cast<std::size_t>(m_GridSize) * m_GridSize, 0);
    std::vector<std::pair<int, int>> stack;
    const RoomCell &start = m_Rooms[0];
    stack.push_back({start.gridX, start.gridY});
    visited[static_cast<std::size_t>(Index(start.gridX, start.gridY))] = 1;
    int reached = 0;
    while (!stack.empty()) {
        const auto [gx, gy] = stack.back();
        stack.pop_back();
        ++reached;
        for (int d = 0; d < 4; ++d) {
            const int nx = gx + kDx[d];
            const int ny = gy + kDy[d];
            if (!HasRoom(nx, ny)) {
                continue;
            }
            const std::size_t idx = static_cast<std::size_t>(Index(nx, ny));
            if (visited[idx] == 0) {
                visited[idx] = 1;
                stack.push_back({nx, ny});
            }
        }
    }
    return reached == static_cast<int>(m_Rooms.size());
}

} // namespace Game
