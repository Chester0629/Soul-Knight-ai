#ifndef GAME_WORLD_ROOMBLOCKERS_HPP
#define GAME_WORLD_ROOMBLOCKERS_HPP

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "data/GameData.hpp"

namespace Game {

/// P2.18: world colliders of room objects, built from the PREFAB colliders (data) instead of a
/// hard-wired 32x32 cell, plus the spawn-point filters that keep bodies out of them.

/// World collider of one prefab blocker (ColliderDef, units, prefab-root space) for an object
/// whose map CELL CENTRE is @p cellCentrePx. FAITHFUL: the prefab root is the cell's
/// bottom-centre (sprite pivot; wall601.prefab collider (0, 0.5) 1x1 sits exactly on the cell),
/// so root = cell centre - (0, cellPx / 2) and the collider centre = root + offset * cellPx.
/// Box -> AABB (size * cellPx), Circle -> circle (radius * cellPx). nullopt for Shape::None or a
/// non-positive extent.
inline std::optional<Util::Collider> BlockerFromDef(const ColliderDef &d, glm::vec2 cellCentrePx,
                                                    float cellPx) {
    const glm::vec2 centre{cellCentrePx.x + d.offsetX * cellPx,
                           cellCentrePx.y + d.offsetY * cellPx - 0.5F * cellPx};
    if (d.shape == ColliderDef::Shape::Box && d.sizeX > 0.0F && d.sizeY > 0.0F) {
        return Util::Collider::MakeAABB(centre, glm::vec2{d.sizeX * cellPx, d.sizeY * cellPx});
    }
    if (d.shape == ColliderDef::Shape::Circle && d.radius > 0.0F) {
        return Util::Collider::MakeCircle(centre, d.radius * cellPx);
    }
    return std::nullopt;
}

/// The first SOLID (non-trigger) blocker of a prefab's list, or nullopt when it has none.
inline std::optional<Util::Collider> FirstBlocker(const std::vector<ColliderDef> &defs,
                                                  glm::vec2 cellCentrePx, float cellPx) {
    for (const ColliderDef &d : defs) {
        if (d.trigger) {
            continue;
        }
        if (auto c = BlockerFromDef(d, cellCentrePx, cellPx)) {
            return c;
        }
    }
    return std::nullopt;
}

/// The blocker collider a design-room obstacle (RGObjectSkin obj_index) contributes, or nullopt
/// when it does not block. @p level supplies the per-level obstacle_list (null = no level data).
///   0        grid wall (collides through the room grid, never here)
///   1..4     boxes / casks      -> the prefab collider (box01..05, cask_*: 1x1 at (0, 0.5))
///   11       brazier            -> the prefab collider
///   8, 9, 10 obj11_* / obj22_* / signs01 props -> the prefab collider when it has one
///            (statue-like 1.5x1.5, 2x1.5, circles; obj11_02 has none and stays decor)
///   5, 6, 7  spikes / speed pads -> trigger colliders only, never block
/// FAITHFUL @ the prefabs' BoxCollider2D / CircleCollider2D (levels.json obstacles[].blockers).
/// PORT CHOICE (T-61): without level data a box / brazier keeps the old 32x32 cell and 8/9/10
/// stay non-blocking (the pre-P2.18 behaviour).
inline std::optional<Util::Collider> DesignObstacleCollider(const LevelDef *level, int objIndex,
                                                            glm::vec2 cellCentrePx, float cellPx) {
    const bool box = objIndex >= 1 && objIndex <= 4;
    const bool brazier = objIndex == 11;
    const bool prop = objIndex >= 8 && objIndex <= 10;
    if (!box && !brazier && !prop) {
        return std::nullopt;
    }
    if (level != nullptr && static_cast<std::size_t>(objIndex) < level->obstacles.size()) {
        return FirstBlocker(level->obstacles[static_cast<std::size_t>(objIndex)].blockers,
                            cellCentrePx, cellPx);
    }
    if (box || brazier) {
        return Util::Collider::MakeAABB(cellCentrePx, glm::vec2{cellPx, cellPx});
    }
    return std::nullopt;
}

/// Spawn / start / boss / drop-point filter: the cells of @p cells (room-local grid coords of a
/// @p w x @p h room centred on @p origin) whose centre is FREE for a body of @p radius, i.e.
/// @p blocks(centre, radius) is false. PORT CHOICE (T-63): RoomGen's floor list ignores the box /
/// brazier overlay (RoomGen.cpp: boxes are "NOT stamped into the grid"), so the port's spawn
/// pool contained cells inside boxes. Order is preserved, so the pool's spread formula
/// (index * stride % size) is the one the scene always used.
template <class BlocksFn>
std::vector<std::pair<int, int>> FilterFreeCells(const std::vector<std::pair<int, int>> &cells,
                                                 int w, int h, float cellPx, glm::vec2 origin,
                                                 float radius, BlocksFn &&blocks) {
    std::vector<std::pair<int, int>> out;
    out.reserve(cells.size());
    for (const auto &c : cells) {
        const glm::vec2 centre{
            (static_cast<float>(c.first) - static_cast<float>(w - 1) * 0.5F) * cellPx + origin.x,
            (static_cast<float>(c.second) - static_cast<float>(h - 1) * 0.5F) * cellPx + origin.y};
        if (!blocks(centre, radius)) {
            out.push_back(c);
        }
    }
    return out;
}

/// Where a body of @p radius that was meant to spawn at @p want actually goes: @p want itself when
/// it is free, else the NEAREST point of @p points that is free for this body (so a big species
/// that does not fit a tight cell uses a roomier one of the same room), else @p want (the caller
/// then depenetrates). PORT CHOICE (T-63).
template <class BlocksFn>
glm::vec2 ResolveSpawnPoint(const std::vector<glm::vec2> &points, glm::vec2 want, float radius,
                            BlocksFn &&blocks) {
    if (!blocks(want, radius)) {
        return want;
    }
    bool found = false;
    float bestSq = 0.0F;
    glm::vec2 best = want;
    for (const glm::vec2 &p : points) {
        const glm::vec2 d = p - want;
        const float dsq = d.x * d.x + d.y * d.y;
        if ((!found || dsq < bestSq) && !blocks(p, radius)) {
            found = true;
            bestSq = dsq;
            best = p;
        }
    }
    return best;
}

} // namespace Game

#endif /* GAME_WORLD_ROOMBLOCKERS_HPP */
