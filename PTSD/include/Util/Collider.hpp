#ifndef UTIL_COLLIDER_HPP
#define UTIL_COLLIDER_HPP

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace Util {

/**
 * @enum ColliderShape
 * @brief The geometric shape backing a Util::Collider.
 *
 * A collider is either an axis-aligned bounding box (AABB) described by its
 * center and half-extents, or a circle described by its center and radius.
 */
enum class ColliderShape {
    AABB,   ///< Axis-aligned bounding box (uses center + halfSize).
    Circle, ///< Circle (uses center + radius).
};

/**
 * @struct Collider
 * @brief A lightweight 2D collision shape in center-origin world space.
 *
 * Positions and extents are expressed with glm::vec2 in pixels, matching the
 * engine's center-origin Cartesian convention (x to the right, y upward). The
 * struct is a plain value type: it owns no resources and is trivially copyable.
 *
 * @note Use the @ref MakeAABB / @ref MakeCircle factory helpers to build a
 * collider with the correct fields populated for the chosen shape.
 */
struct Collider {
    /**
     * @brief The shape selector for this collider.
     */
    ColliderShape shape{ColliderShape::AABB};

    /**
     * @brief The center of the collider in world space.
     */
    glm::vec2 center{0.0f, 0.0f};

    /**
     * @brief The AABB half-extents (half width, half height).
     *
     * Only meaningful when @ref shape is ColliderShape::AABB.
     */
    glm::vec2 halfSize{0.0f, 0.0f};

    /**
     * @brief The circle radius.
     *
     * Only meaningful when @ref shape is ColliderShape::Circle.
     */
    float radius{0.0f};

    /**
     * @brief Creates an axis-aligned bounding box collider.
     *
     * @param center The center of the box in world space.
     * @param size   The full width and height of the box.
     * @return A Collider configured as an AABB.
     */
    static Collider MakeAABB(glm::vec2 center, glm::vec2 size) {
        Collider c;
        c.shape = ColliderShape::AABB;
        c.center = center;
        c.halfSize = size * 0.5f;
        c.radius = 0.0f;
        return c;
    }

    /**
     * @brief Creates a circle collider.
     *
     * @param center The center of the circle in world space.
     * @param radius The radius of the circle.
     * @return A Collider configured as a circle.
     */
    static Collider MakeCircle(glm::vec2 center, float radius) {
        Collider c;
        c.shape = ColliderShape::Circle;
        c.center = center;
        c.halfSize = glm::vec2{0.0f, 0.0f};
        c.radius = radius;
        return c;
    }

    /**
     * @brief Returns the minimum corner of the collider's bounding box.
     *
     * For an AABB this is the bottom-left corner. For a circle this is a
     * conservative bounding box corner (center - radius on both axes), which is
     * convenient for broadphase cell insertion.
     *
     * @return The minimum (x, y) world-space corner.
     */
    glm::vec2 Min() const {
        if (shape == ColliderShape::Circle) {
            return center - glm::vec2{radius, radius};
        }
        return center - halfSize;
    }

    /**
     * @brief Returns the maximum corner of the collider's bounding box.
     *
     * For an AABB this is the top-right corner. For a circle this is a
     * conservative bounding box corner (center + radius on both axes).
     *
     * @return The maximum (x, y) world-space corner.
     */
    glm::vec2 Max() const {
        if (shape == ColliderShape::Circle) {
            return center + glm::vec2{radius, radius};
        }
        return center + halfSize;
    }
};

namespace Detail {

/**
 * @brief Tests two axis-aligned bounding boxes for overlap.
 *
 * Touching edges (zero-gap) are treated as overlapping.
 */
inline bool OverlapAABB(const Collider &a, const Collider &b) {
    const glm::vec2 aMin = a.Min();
    const glm::vec2 aMax = a.Max();
    const glm::vec2 bMin = b.Min();
    const glm::vec2 bMax = b.Max();
    return aMin.x <= bMax.x && aMax.x >= bMin.x && aMin.y <= bMax.y &&
           aMax.y >= bMin.y;
}

/**
 * @brief Tests two circles for overlap using squared distance.
 *
 * Touching circles (distance == sum of radii) are treated as overlapping.
 */
inline bool OverlapCircle(const Collider &a, const Collider &b) {
    const glm::vec2 d = b.center - a.center;
    const float distSq = d.x * d.x + d.y * d.y;
    const float r = a.radius + b.radius;
    return distSq <= r * r;
}

/**
 * @brief Tests an AABB against a circle for overlap.
 *
 * @param box    A collider whose shape is ColliderShape::AABB.
 * @param circle A collider whose shape is ColliderShape::Circle.
 */
inline bool OverlapAABBCircle(const Collider &box, const Collider &circle) {
    const glm::vec2 boxMin = box.Min();
    const glm::vec2 boxMax = box.Max();
    const float cx = std::clamp(circle.center.x, boxMin.x, boxMax.x);
    const float cy = std::clamp(circle.center.y, boxMin.y, boxMax.y);
    const float dx = circle.center.x - cx;
    const float dy = circle.center.y - cy;
    return (dx * dx + dy * dy) <= circle.radius * circle.radius;
}

} // namespace Detail

/**
 * @brief Narrowphase overlap test between any two colliders.
 *
 * Handles all combinations of AABB and Circle shapes (including both argument
 * orders of the mixed case). Touching shapes are considered overlapping.
 *
 * @param a The first collider.
 * @param b The second collider.
 * @return true if the two colliders overlap or touch, false otherwise.
 */
inline bool Overlap(const Collider &a, const Collider &b) {
    if (a.shape == ColliderShape::AABB && b.shape == ColliderShape::AABB) {
        return Detail::OverlapAABB(a, b);
    }
    if (a.shape == ColliderShape::Circle && b.shape == ColliderShape::Circle) {
        return Detail::OverlapCircle(a, b);
    }
    if (a.shape == ColliderShape::AABB) {
        return Detail::OverlapAABBCircle(a, b);
    }
    return Detail::OverlapAABBCircle(b, a);
}

/**
 * @brief Computes the minimum translation vector (MTV) to separate @p a from
 * @p b.
 *
 * The returned vector, when added to @p a's center, moves @p a just clear of
 * @p b along the axis of least penetration. If the colliders do not overlap the
 * result is the zero vector.
 *
 * @param a The collider to be separated (the one that would move).
 * @param b The collider to separate from.
 * @return The translation to apply to @p a, or {0, 0} if not overlapping.
 */
inline glm::vec2 ResolveMTV(const Collider &a, const Collider &b) {
    if (!Overlap(a, b)) {
        return glm::vec2{0.0f, 0.0f};
    }

    // AABB vs AABB: least-penetration axis.
    if (a.shape == ColliderShape::AABB && b.shape == ColliderShape::AABB) {
        const glm::vec2 d = a.center - b.center;
        const glm::vec2 overlap = (a.halfSize + b.halfSize) -
                                  glm::vec2{std::abs(d.x), std::abs(d.y)};
        if (overlap.x < overlap.y) {
            const float sign = d.x < 0.0f ? -1.0f : 1.0f;
            return glm::vec2{overlap.x * sign, 0.0f};
        }
        const float sign = d.y < 0.0f ? -1.0f : 1.0f;
        return glm::vec2{0.0f, overlap.y * sign};
    }

    // Circle vs Circle: push along the center-to-center axis.
    if (a.shape == ColliderShape::Circle && b.shape == ColliderShape::Circle) {
        const glm::vec2 d = a.center - b.center;
        const float dist = std::sqrt(d.x * d.x + d.y * d.y);
        const float pen = (a.radius + b.radius) - dist;
        if (dist > 0.0f) {
            return (d / dist) * pen;
        }
        // Coincident centers: pick a deterministic axis.
        return glm::vec2{pen, 0.0f};
    }

    // Mixed AABB / Circle: push the circle out of the box, then orient the
    // result so it separates @p a from @p b regardless of argument order.
    const bool aIsBox = a.shape == ColliderShape::AABB;
    const Collider &box = aIsBox ? a : b;
    const Collider &circle = aIsBox ? b : a;

    const glm::vec2 boxMin = box.Min();
    const glm::vec2 boxMax = box.Max();
    const float cx = std::clamp(circle.center.x, boxMin.x, boxMax.x);
    const float cy = std::clamp(circle.center.y, boxMin.y, boxMax.y);
    const glm::vec2 closest{cx, cy};
    glm::vec2 d = circle.center - closest;
    float dist = std::sqrt(d.x * d.x + d.y * d.y);

    glm::vec2 pushCircle{0.0f, 0.0f};
    if (dist > 0.0f) {
        // Circle center outside the box: push along the outward normal.
        pushCircle = (d / dist) * (circle.radius - dist);
    } else {
        // Circle center inside the box: escape via the nearest face.
        const float left = circle.center.x - boxMin.x;
        const float right = boxMax.x - circle.center.x;
        const float bottom = circle.center.y - boxMin.y;
        const float top = boxMax.y - circle.center.y;
        const float minOverlap = std::min({left, right, bottom, top});
        if (minOverlap == left) {
            pushCircle = glm::vec2{-(left + circle.radius), 0.0f};
        } else if (minOverlap == right) {
            pushCircle = glm::vec2{right + circle.radius, 0.0f};
        } else if (minOverlap == bottom) {
            pushCircle = glm::vec2{0.0f, -(bottom + circle.radius)};
        } else {
            pushCircle = glm::vec2{0.0f, top + circle.radius};
        }
    }

    // pushCircle moves the circle. If @p a is the circle, that is what we want;
    // otherwise @p a is the box and must move the opposite way.
    return aIsBox ? -pushCircle : pushCircle;
}

} // namespace Util

#endif /* UTIL_COLLIDER_HPP */
