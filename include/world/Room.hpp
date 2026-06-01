#ifndef GAME_ROOM_HPP
#define GAME_ROOM_HPP

#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"

namespace Game {

/**
 * @class Room
 * @brief A rectangular room described purely by its four boundary wall
 *        colliders.
 *
 * A Room is not a Util::GameObject: it carries no drawable and is never added
 * to the scene graph. It is collision data only. The constructor builds four
 * axis-aligned bounding box walls that enclose a centered interior rectangle,
 * each wall placed just outside the interior so that the playable area exactly
 * matches the requested @p size.
 */
class Room {
public:
    /**
     * @brief Builds the four boundary walls enclosing a centered rectangle.
     *
     * The top and bottom walls span the full interior width plus the wall
     * thickness on each side; the left and right walls span the interior
     * height. Each wall is offset from the center by half the interior size
     * plus half the wall thickness, so the inner face of every wall lies on the
     * edge of the interior rectangle.
     *
     * @param center    The world-space center of the interior rectangle.
     * @param size      The full interior width and height (playable area).
     * @param thickness The thickness of each boundary wall.
     */
    Room(glm::vec2 center, glm::vec2 size, float thickness);

    /**
     * @brief Returns the four boundary wall colliders.
     *
     * @return A const reference to the wall collider list (always four AABBs).
     */
    const std::vector<Util::Collider> &Walls() const;

    /**
     * @brief Returns the world-space center of the interior rectangle.
     *
     * @return The center passed to the constructor.
     */
    glm::vec2 Center() const;

    /**
     * @brief Returns the full interior size (width, height).
     *
     * @return The size passed to the constructor.
     */
    glm::vec2 Size() const;

    /**
     * @brief Tests whether a circle overlaps any boundary wall.
     *
     * Uses a circle-vs-AABB closest-point test against each wall: the circle
     * blocks if the distance from its center to the nearest point on any wall
     * is at most @p radius.
     *
     * @param pos    The world-space center of the circle.
     * @param radius The radius of the circle.
     * @return true if the circle overlaps or touches any wall, false otherwise.
     */
    bool Blocks(glm::vec2 pos, float radius) const;

private:
    std::vector<Util::Collider> m_Walls;
    glm::vec2 m_Center{0.0f, 0.0f};
    glm::vec2 m_Size{0.0f, 0.0f};
};

} // namespace Game

#endif /* GAME_ROOM_HPP */
