#ifndef GAME_PLAYER_HPP
#define GAME_PLAYER_HPP

#include <memory>
#include <string>
#include <utility>

#include <glm/glm.hpp>

#include "Util/Animation.hpp"
#include "Util/GameObject.hpp"

#include "combat/CombatStats.hpp"
#include "data/GameData.hpp"
#include "entities/SpriteAnchor.hpp"

namespace Game {
/**
 * @class Player
 * @brief The player-controlled hero entity.
 *
 * Renders the c01 character animation and moves with WASD at a speed derived
 * from the character definition (characters.json speed, Unity units/s, times
 * Sim::kDataSpeedToPxPerSec = 32 px per unit). Carries its CombatStats (hp/armor/energy).
 * World space is center-origin Cartesian (y up), matching the engine.
 */
class Player : public Util::GameObject {
public:
    Player(const CharacterDef &def, const std::string &resourceRoot,
           std::vector<std::string> frames = {}, int frameIntervalMs = 80);

    /// WASD movement for this step.
    void Update(float dtMs) override;

    glm::vec2 Position() const { return m_Transform.translation; }
    const CombatStats &Stats() const { return m_Stats; }
    CombatStats &Stats() { return m_Stats; }

    /// Install the data-driven sprite anchor (pivots / feet offset / body scale) and
    /// re-apply it. See SpriteAnchor for the FAITHFUL scale + pivot rule.
    void SetAnchor(SpriteAnchor anchor) {
        m_Anchor = std::move(anchor);
        SyncSprite();
    }
    /// Apply scale + the current frame's pivot (called each frame before the draw).
    void SyncSprite() { m_Anchor.Apply(*this, *m_Anim); }
    /// Physical body radius (px) from the hero prefab's "collider" child.
    float BodyRadiusPx() const { return m_Anchor.bodyRadiusPx; }
    /// Walk speed (px/s) -- read by the SK_WALK_DIR test hook.
    float Speed() const { return m_Speed; }

private:
    SpriteAnchor m_Anchor;
    CombatStats m_Stats;
    float m_Speed; ///< pixels per second
    std::shared_ptr<Util::Animation> m_Anim;
};
} // namespace Game

#endif /* GAME_PLAYER_HPP */
