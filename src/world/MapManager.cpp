#include "world/MapManager.hpp"

#include <algorithm>
#include <vector>

namespace Game {

namespace {
// Direction convention of the original walk: 0=up(+y) 1=down(-y) 2=left(-x)
// 3=right(+x). dx/dy index by that code.
constexpr int kDx[4] = {0, 0, -1, 1};
constexpr int kDy[4] = {1, -1, 0, 0};
constexpr int kWalkGuard = 1000; ///< matches the original's guard cap.
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

    // Derive each room's door flags from its placed orthogonal neighbours, in
    // RoomGen's entrance order: [0]=EAST(+x) [1]=NORTH(+y) [2]=WEST(-x) [3]=SOUTH(-y).
    for (RoomCell &room : m_Rooms) {
        room.entrance[0] = HasRoom(room.gridX + 1, room.gridY) ? 1 : 0;
        room.entrance[1] = HasRoom(room.gridX, room.gridY + 1) ? 1 : 0;
        room.entrance[2] = HasRoom(room.gridX - 1, room.gridY) ? 1 : 0;
        room.entrance[3] = HasRoom(room.gridX, room.gridY - 1) ? 1 : 0;
    }
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
