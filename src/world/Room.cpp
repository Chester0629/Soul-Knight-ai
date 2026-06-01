#include "world/Room.hpp"

namespace Game {

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
