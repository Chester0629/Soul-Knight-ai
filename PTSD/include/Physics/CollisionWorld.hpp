#ifndef PHYSICS_COLLISION_WORLD_HPP
#define PHYSICS_COLLISION_WORLD_HPP

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"

namespace Physics {

/**
 * @class CollisionWorld
 * @brief A spatial-hash broadphase plus narrowphase collision world.
 *
 * The world stores 2D colliders (see Util::Collider) keyed by stable, opaque
 * handles. Colliders are bucketed into a uniform grid of square cells; each
 * collider is inserted into every cell its bounding box overlaps. Queries,
 * pair generation, and raycasts use the grid to gather a small set of
 * candidates and then confirm them with the exact narrowphase test
 * Util::Overlap.
 *
 * All operations are deterministic: handles come from a monotonic counter,
 * candidate sets are deduplicated, and every result list is sorted. There is no
 * use of randomness or wall-clock time.
 */
class CollisionWorld {
public:
    /**
     * @brief A stable, opaque identifier for a collider in the world.
     */
    using Handle = std::uint32_t;

    /**
     * @struct RayHit
     * @brief The result of a @ref Raycast.
     */
    struct RayHit {
        bool hit{false};               ///< Whether the ray hit any collider.
        Handle handle{0};              ///< The handle that was hit (if any).
        float t{0.0f};                 ///< Distance along the ray to the hit.
        glm::vec2 point{0.0f, 0.0f};   ///< World-space hit point.
    };

    /**
     * @brief Constructs an empty collision world.
     *
     * @param cellSize The edge length of each spatial-hash cell, in pixels.
     *                 Larger cells mean fewer buckets but more candidates per
     *                 cell. Must be positive; non-positive values fall back to a
     *                 default of 64.
     */
    explicit CollisionWorld(float cellSize = 64.0f);

    /**
     * @brief Inserts a collider into the world.
     *
     * @param c        The collider geometry to add.
     * @param userData An opaque value associated with the collider, returned by
     *                 @ref GetUserData. Defaults to 0.
     * @return A fresh handle that identifies the collider.
     */
    Handle Add(const Util::Collider &c, std::uint32_t userData = 0);

    /**
     * @brief Replaces the geometry of an existing collider.
     *
     * The associated user data is preserved. If @p h is not a live handle the
     * call is a no-op.
     *
     * @param h The handle to update.
     * @param c The new collider geometry.
     */
    void Update(Handle h, const Util::Collider &c);

    /**
     * @brief Removes a collider from the world.
     *
     * The handle is retired and will not be reused. If @p h is not live the
     * call is a no-op.
     *
     * @param h The handle to remove.
     */
    void Remove(Handle h);

    /**
     * @brief Returns the user data associated with a handle.
     *
     * @param h The handle to look up.
     * @return The stored user data, or 0 if @p h is not live.
     */
    std::uint32_t GetUserData(Handle h) const;

    /**
     * @brief Returns the number of live colliders in the world.
     *
     * @return The live collider count.
     */
    std::size_t Size() const;

    /**
     * @brief Finds all colliders overlapping a query area.
     *
     * Gathers broadphase candidates from the cells the area's bounding box
     * touches, then confirms each with Util::Overlap. The query area itself is
     * not part of the world, so no self-match is possible.
     *
     * @param area The collider describing the region to query.
     * @return The handles whose colliders overlap @p area, sorted ascending.
     */
    std::vector<Handle> Query(const Util::Collider &area) const;

    /**
     * @brief Computes all unique overlapping handle pairs.
     *
     * Each pair (a, b) satisfies a < b, is reported at most once, and is
     * narrowphase-confirmed with Util::Overlap. The output is sorted
     * lexicographically for determinism.
     *
     * @return The confirmed overlapping handle pairs.
     */
    std::vector<std::pair<Handle, Handle>> ComputePairs() const;

    /**
     * @brief Casts a ray and returns the nearest collider it hits.
     *
     * Uses a slab test for AABB colliders and an analytic ray-circle test for
     * circle colliders. Only hits with a parametric distance @c t in the range
     * [0, @p maxDist] are considered.
     *
     * @param origin  The ray origin in world space.
     * @param dir     The ray direction. It need not be normalized; @c t is
     *                measured in units of @p dir length, and @p maxDist is in
     *                the same units.
     * @param maxDist The maximum distance to search along the ray.
     * @return A RayHit describing the nearest intersection, or a RayHit with
     *         @c hit == false if nothing was hit.
     */
    RayHit Raycast(glm::vec2 origin, glm::vec2 dir, float maxDist) const;

private:
    /// Integer grid coordinate of a cell.
    struct CellCoord {
        std::int32_t x{0};
        std::int32_t y{0};

        bool operator==(const CellCoord &o) const {
            return x == o.x && y == o.y;
        }
    };

    /// Hash functor for CellCoord usable as an unordered_map key.
    struct CellHash {
        std::size_t operator()(const CellCoord &c) const {
            const std::uint64_t ux =
                static_cast<std::uint32_t>(c.x);
            const std::uint64_t uy =
                static_cast<std::uint32_t>(c.y);
            return static_cast<std::size_t>((ux * 0x9E3779B97F4A7C15ull) ^
                                            (uy + 0x165667B19E3779F9ull));
        }
    };

    /// One stored collider plus its metadata.
    struct Entry {
        Util::Collider collider{};
        std::uint32_t userData{0};
        bool live{false};
    };

    /// Computes the inclusive cell-coordinate range a bounding box spans.
    void CellRange(const Util::Collider &c, CellCoord &lo, CellCoord &hi) const;

    /// Inserts the handle into every cell its collider overlaps.
    void InsertIntoGrid(Handle h);

    /// Removes the handle from every cell its collider overlaps.
    void RemoveFromGrid(Handle h);

    float m_cellSize;
    Handle m_nextHandle{1};
    std::unordered_map<Handle, Entry> m_entries;
    std::unordered_map<CellCoord, std::vector<Handle>, CellHash> m_cells;
};

} // namespace Physics

#endif /* PHYSICS_COLLISION_WORLD_HPP */
