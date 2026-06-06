#include "world/Room.hpp"

#include <utility>

namespace Game {

Room::Room(std::vector<Util::Collider> walls, glm::vec2 center, glm::vec2 size)
    : m_Walls(std::move(walls)), m_Center(center), m_Size(size) {}

bool Room::IsSolidCell(int code) {
    // Walkable: floor (0), aisle (-2), door (11). Everything else blocks:
    // border (-1), big obstacle (1), destructible (2), markers (8/9), decor (>2).
    return code != 0 && code != -2 && code != 11;
}

glm::vec2 Room::CellToWorld(int x, int y, int width, int height, float cellSize,
                            glm::vec2 origin) {
    const float cx = (static_cast<float>(x) - static_cast<float>(width - 1) * 0.5F) * cellSize;
    const float cy = (static_cast<float>(y) - static_cast<float>(height - 1) * 0.5F) * cellSize;
    return glm::vec2{cx, cy} + origin;
}

Room Room::FromRoomGen(const RoomGen &gen, float cellSize, glm::vec2 origin) {
    const int w = gen.Width();
    const int h = gen.Height();
    std::vector<Util::Collider> walls;
    const glm::vec2 cell{cellSize, cellSize};
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            if (IsSolidCell(gen.At(x, y))) {
                walls.push_back(Util::Collider::MakeAABB(
                    CellToWorld(x, y, w, h, cellSize, origin), cell));
            }
        }
    }
    const glm::vec2 size{static_cast<float>(w) * cellSize,
                         static_cast<float>(h) * cellSize};
    return Room{std::move(walls), origin, size};
}

Room::Room(glm::vec2 center, glm::vec2 size, float thickness)
    : m_Center(center), m_Size(size) {
    const float halfW = size.x * 0.5f;
    const float halfH = size.y * 0.5f;
    const float halfT = thickness * 0.5f;

    // Top and bottom walls span the full interior width plus a wall thickness
    // on each side so their corners meet the side walls cleanly.
    const glm::vec2 horizontalSize{size.x + thickness * 2.0f, thickness};
    // Left and right walls span exactly the interior height.
    const glm::vec2 verticalSize{thickness, size.y};

    const float topY = center.y + halfH + halfT;
    const float bottomY = center.y - halfH - halfT;
    const float rightX = center.x + halfW + halfT;
    const float leftX = center.x - halfW - halfT;

    m_Walls.reserve(4);
    m_Walls.push_back(Util::Collider::MakeAABB(
        glm::vec2{center.x, topY}, horizontalSize));
    m_Walls.push_back(Util::Collider::MakeAABB(
        glm::vec2{center.x, bottomY}, horizontalSize));
    m_Walls.push_back(Util::Collider::MakeAABB(
        glm::vec2{leftX, center.y}, verticalSize));
    m_Walls.push_back(Util::Collider::MakeAABB(
        glm::vec2{rightX, center.y}, verticalSize));
}

const std::vector<Util::Collider> &Room::Walls() const {
    return m_Walls;
}

glm::vec2 Room::Center() const {
    return m_Center;
}

glm::vec2 Room::Size() const {
    return m_Size;
}

bool Room::Blocks(glm::vec2 pos, float radius) const {
    const Util::Collider circle = Util::Collider::MakeCircle(pos, radius);
    for (const Util::Collider &wall : m_Walls) {
        if (Util::Overlap(wall, circle)) {
            return true;
        }
    }
    return false;
}

} // namespace Game
