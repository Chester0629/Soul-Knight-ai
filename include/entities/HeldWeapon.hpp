#ifndef GAME_ENTITIES_HELDWEAPON_HPP
#define GAME_ENTITIES_HELDWEAPON_HPP

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Util/GameObject.hpp"
#include "Util/Image.hpp"

#include "data/GameData.hpp"

namespace Game {

/**
 * @class HeldWeapon
 * @brief Slice 4: the weapon a hero holds, drawn on its hand mount.
 *
 * FAITHFUL @ the weapon prefab + the hero prefab: the weapon (weapon_000: child `w` = weapons_19, sprite
 * pivot (0, 0.35) = the grip, `gun_point` (0.6, 0.15)) hangs under the hand mount h1 (c01: (0, 0.5)
 * under `img`; the h1 RGHand component carries target_layer 6, h2 carries 4). The sprite rotates about the
 * grip to the aim and mirrors in x with the hero's facing -- pose maths in game/ActorPose.hpp
 * (unit-tested), drawn here through Util::GameObject (T * R * S about the pivot).
 * UNVERIFIED: that RGHand gives its weapon the sorting layer target_layer (RGHand$$SetUpWeapon 004f9120 is
 * truncated at a GetComponent tail call). The port reads it that way because the ground-truth frames draw the
 * front gun OVER the body (layer 5) in every aim, which the weapon prefab's own layer 5 / order 1 (below the
 * body's order 5) cannot produce.
 */
class HeldWeapon {
public:
    /// Dress the view as @p def (null / no held sprite = hidden). @p handLayer is the h1 target layer.
    void SetWeapon(const WeaponDef *def, const std::string &resourceRoot, int handLayer);
    bool Visible() const { return m_Object != nullptr && m_Visible; }
    /// The GameObject to attach to the hero (null until a weapon with a held sprite was set).
    const std::shared_ptr<Util::GameObject> &Object() const { return m_Object; }

    /// Place the sprite for a hero whose body centre is @p position, facing @p facing, aiming
    /// @p aim (unit). @p handRootPx / @p feetOffsetPx are the prefab hand mount and Position()-feet.
    void Sync(glm::vec2 position, glm::vec2 aim, int facing, glm::vec2 handRootPx, glm::vec2 feetOffsetPx);
    /// World position of the muzzle (gun_point) for the same pose; @p position when there is none.
    glm::vec2 Muzzle(glm::vec2 position, glm::vec2 aim, int facing, glm::vec2 handRootPx,
                     glm::vec2 feetOffsetPx) const;
    /// Draw z of the held sprite (SortZ of the hand layer and the sprite's order).
    float Z() const { return m_Object != nullptr ? m_Object->GetZIndex() : 0.0F; }

private:
    HeldWeaponDef m_Def;
    std::shared_ptr<Util::GameObject> m_Object;
    std::shared_ptr<Util::Image> m_Image;
    bool m_Visible = false;
};

} // namespace Game

#endif /* GAME_ENTITIES_HELDWEAPON_HPP */
