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
#include "entities/ActorVisual.hpp"
#include "entities/HeldWeapon.hpp"
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
        m_Vis.SetAnchor(std::move(anchor));
        SyncSprite();
    }
    /// Apply the clip, facing mirror, scale + pivot, shadow / ring and the held weapon (called each
    /// frame before the draw).
    void SyncSprite();
    /// Physical body radius (px) from the hero prefab's "collider" child.
    float BodyRadiusPx() const { return m_Vis.Anchor().bodyRadiusPx; }
    /// Walk speed (px/s) -- read by the SK_WALK_DIR test hook.
    float Speed() const { return m_Speed; }

    // --- SLICE 4 (characters): clip / facing / held weapon / shadow + ring ---
    /// The clip + shadow + ring helper (run / dead clips are registered through it by the scene).
    ActorVisual &Visual() { return m_Vis; }
    const ActorVisual &Visual() const { return m_Vis; }
    /// Install the data hand mount h1 (prefab-root px, root = feet) and the sorting layer of its weapon.
    void SetHand(glm::vec2 handRootPx, int handLayer) {
        m_HandRootPx = handRootPx;
        m_HandLayer = handLayer;
    }
    glm::vec2 HandRootPx() const { return m_HandRootPx; }
    /// Dress the held weapon (null / no held sprite = none); attaches the drawable once.
    void SetHeldWeapon(const WeaponDef *def, const std::string &resourceRoot);
    const HeldWeapon &Held() const { return m_Held; }
    /// Per-frame pose: the aim (unit), the facing (+1 / -1, Sim::PlayerFacing), the run clip while
    /// @p moving, and the ring colour for the skill state. Re-syncs the sprite.
    void UpdatePose(glm::vec2 aim, int facing, bool moving, bool skillReady);
    /// World muzzle (gun_point) of the held weapon for the last pose (Position() with no weapon).
    glm::vec2 Muzzle() const;
    glm::vec2 Aim() const { return m_Aim; }

private:
    ActorVisual m_Vis;
    HeldWeapon m_Held;
    bool m_HeldAttached = false;
    glm::vec2 m_HandRootPx{0.0F, 0.0F};
    int m_HandLayer = 6;
    glm::vec2 m_Aim{1.0F, 0.0F};
    CombatStats m_Stats;
    float m_Speed; ///< pixels per second
    std::shared_ptr<Util::Animation> m_Anim;
};
} // namespace Game

#endif /* GAME_PLAYER_HPP */
