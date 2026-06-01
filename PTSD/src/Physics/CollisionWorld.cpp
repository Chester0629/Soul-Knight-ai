#include "Physics/CollisionWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace Physics {

CollisionWorld::CollisionWorld(float cellSize)
    : m_cellSize(cellSize > 0.0f ? cellSize : 64.0f) {}

void CollisionWorld::CellRange(const Util::Collider &c, CellCoord &lo,
                               CellCoord &hi) const {
    const glm::vec2 mn = c.Min();
    const glm::vec2 mx = c.Max();
    lo.x = static_cast<std::int32_t>(std::floor(mn.x / m_cellSize));
    lo.y = static_cast<std::int32_t>(std::floor(mn.y / m_cellSize));
    hi.x = static_cast<std::int32_t>(std::floor(mx.x / m_cellSize));
    hi.y = static_cast<std::int32_t>(std::floor(mx.y / m_cellSize));
}

void CollisionWorld::InsertIntoGrid(Handle h) {
    const auto it = m_entries.find(h);
    if (it == m_entries.end() || !it->second.live) {
        return;
    }
    CellCoord lo;
    CellCoord hi;
    CellRange(it->second.collider, lo, hi);
    for (std::int32_t cy = lo.y; cy <= hi.y; ++cy) {
        for (std::int32_t cx = lo.x; cx <= hi.x; ++cx) {
            m_cells[CellCoord{cx, cy}].push_back(h);
        }
    }
}

void CollisionWorld::RemoveFromGrid(Handle h) {
    const auto it = m_entries.find(h);
    if (it == m_entries.end()) {
        return;
    }
    CellCoord lo;
    CellCoord hi;
    CellRange(it->second.collider, lo, hi);
    for (std::int32_t cy = lo.y; cy <= hi.y; ++cy) {
        for (std::int32_t cx = lo.x; cx <= hi.x; ++cx) {
            const auto cellIt = m_cells.find(CellCoord{cx, cy});
            if (cellIt == m_cells.end()) {
                continue;
            }
            std::vector<Handle> &bucket = cellIt->second;
            bucket.erase(std::remove(bucket.begin(), bucket.end(), h),
                         bucket.end());
            if (bucket.empty()) {
                m_cells.erase(cellIt);
            }
        }
    }
}

CollisionWorld::Handle CollisionWorld::Add(const Util::Collider &c,
                                           std::uint32_t userData) {
    const Handle h = m_nextHandle++;
    Entry e;
    e.collider = c;
    e.userData = userData;
    e.live = true;
    m_entries.emplace(h, e);
    InsertIntoGrid(h);
    return h;
}

void CollisionWorld::Update(Handle h, const Util::Collider &c) {
    const auto it = m_entries.find(h);
    if (it == m_entries.end() || !it->second.live) {
        return;
    }
    RemoveFromGrid(h);
    it->second.collider = c;
    InsertIntoGrid(h);
}

void CollisionWorld::Remove(Handle h) {
    const auto it = m_entries.find(h);
    if (it == m_entries.end() || !it->second.live) {
        return;
    }
    RemoveFromGrid(h);
    m_entries.erase(it);
}

std::uint32_t CollisionWorld::GetUserData(Handle h) const {
    const auto it = m_entries.find(h);
    if (it == m_entries.end() || !it->second.live) {
        return 0;
    }
    return it->second.userData;
}

std::size_t CollisionWorld::Size() const { return m_entries.size(); }

std::vector<CollisionWorld::Handle>
CollisionWorld::Query(const Util::Collider &area) const {
    CellCoord lo;
    CellCoord hi;
    CellRange(area, lo, hi);

    std::unordered_set<Handle> seen;
    std::vector<Handle> result;
    for (std::int32_t cy = lo.y; cy <= hi.y; ++cy) {
        for (std::int32_t cx = lo.x; cx <= hi.x; ++cx) {
            const auto cellIt = m_cells.find(CellCoord{cx, cy});
            if (cellIt == m_cells.end()) {
                continue;
            }
            for (const Handle h : cellIt->second) {
                if (!seen.insert(h).second) {
                    continue;
                }
                const auto entryIt = m_entries.find(h);
                if (entryIt == m_entries.end() || !entryIt->second.live) {
                    continue;
                }
                if (Util::Overlap(area, entryIt->second.collider)) {
                    result.push_back(h);
                }
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::pair<CollisionWorld::Handle, CollisionWorld::Handle>>
CollisionWorld::ComputePairs() const {
    std::vector<std::pair<Handle, Handle>> pairs;
    std::unordered_set<std::uint64_t> seenPairs;

    for (const auto &cellEntry : m_cells) {
        const std::vector<Handle> &bucket = cellEntry.second;
        for (std::size_t i = 0; i < bucket.size(); ++i) {
            for (std::size_t j = i + 1; j < bucket.size(); ++j) {
                Handle a = bucket[i];
                Handle b = bucket[j];
                if (a == b) {
                    continue;
                }
                if (a > b) {
                    std::swap(a, b);
                }
                const std::uint64_t key =
                    (static_cast<std::uint64_t>(a) << 32) |
                    static_cast<std::uint64_t>(b);
                if (!seenPairs.insert(key).second) {
                    continue;
                }
                const auto ea = m_entries.find(a);
                const auto eb = m_entries.find(b);
                if (ea == m_entries.end() || eb == m_entries.end() ||
                    !ea->second.live || !eb->second.live) {
                    continue;
                }
                if (Util::Overlap(ea->second.collider, eb->second.collider)) {
                    pairs.emplace_back(a, b);
                }
            }
        }
    }

    std::sort(pairs.begin(), pairs.end());
    return pairs;
}

namespace {

/**
 * @brief Ray vs axis-aligned bounding box using the slab method.
 *
 * @param origin  Ray origin.
 * @param dir     Ray direction (need not be unit length).
 * @param mn      Box minimum corner.
 * @param mx      Box maximum corner.
 * @param maxDist Maximum parametric distance to accept.
 * @param outT    Parametric distance of the entry hit, written on success.
 * @return true if the ray enters the box within [0, maxDist].
 */
bool RayAABB(glm::vec2 origin, glm::vec2 dir, glm::vec2 mn, glm::vec2 mx,
             float maxDist, float &outT) {
    float tMin = 0.0f;
    float tMax = maxDist;

    for (int axis = 0; axis < 2; ++axis) {
        const float o = origin[axis];
        const float d = dir[axis];
        const float lo = mn[axis];
        const float hi = mx[axis];
        if (std::abs(d) < std::numeric_limits<float>::epsilon()) {
            // Ray is parallel to this slab: miss if origin is outside it.
            if (o < lo || o > hi) {
                return false;
            }
        } else {
            const float inv = 1.0f / d;
            float t1 = (lo - o) * inv;
            float t2 = (hi - o) * inv;
            if (t1 > t2) {
                std::swap(t1, t2);
            }
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) {
                return false;
            }
        }
    }
    outT = tMin;
    return true;
}

/**
 * @brief Ray vs circle, returning the nearest non-negative entry distance.
 *
 * @param origin  Ray origin.
 * @param dir     Ray direction (need not be unit length).
 * @param center  Circle center.
 * @param radius  Circle radius.
 * @param maxDist Maximum parametric distance to accept.
 * @param outT    Parametric distance of the hit, written on success.
 * @return true if the ray intersects the circle within [0, maxDist].
 */
bool RayCircle(glm::vec2 origin, glm::vec2 dir, glm::vec2 center, float radius,
               float maxDist, float &outT) {
    const glm::vec2 m = origin - center;
    const float a = dir.x * dir.x + dir.y * dir.y;
    if (a < std::numeric_limits<float>::epsilon()) {
        return false;
    }
    const float b = 2.0f * (m.x * dir.x + m.y * dir.y);
    const float c = (m.x * m.x + m.y * m.y) - radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) {
        return false;
    }
    const float sqrtDisc = std::sqrt(disc);
    const float inv2a = 1.0f / (2.0f * a);
    float t = (-b - sqrtDisc) * inv2a;
    if (t < 0.0f) {
        // Origin is inside the circle: use the far root (entry at t == 0).
        t = (-b + sqrtDisc) * inv2a;
        if (t < 0.0f) {
            return false;
        }
        t = 0.0f;
    }
    if (t > maxDist) {
        return false;
    }
    outT = t;
    return true;
}

} // namespace

CollisionWorld::RayHit CollisionWorld::Raycast(glm::vec2 origin, glm::vec2 dir,
                                               float maxDist) const {
    RayHit best;
    float bestT = std::numeric_limits<float>::max();

    // Brute-force narrowphase over candidates is acceptable here; the broadphase
    // grid is used by Query/ComputePairs. We iterate live entries in handle
    // order so ties resolve deterministically.
    std::vector<Handle> handles;
    handles.reserve(m_entries.size());
    for (const auto &kv : m_entries) {
        if (kv.second.live) {
            handles.push_back(kv.first);
        }
    }
    std::sort(handles.begin(), handles.end());

    for (const Handle h : handles) {
        const Entry &e = m_entries.at(h);
        float t = 0.0f;
        bool hit = false;
        if (e.collider.shape == Util::ColliderShape::AABB) {
            hit = RayAABB(origin, dir, e.collider.Min(), e.collider.Max(),
                          maxDist, t);
        } else {
            hit = RayCircle(origin, dir, e.collider.center, e.collider.radius,
                            maxDist, t);
        }
        if (hit && t < bestT) {
            bestT = t;
            best.hit = true;
            best.handle = h;
            best.t = t;
            best.point = origin + dir * t;
        }
    }

    return best;
}

} // namespace Physics
