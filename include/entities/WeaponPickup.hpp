#ifndef GAME_WEAPON_PICKUP_HPP
#define GAME_WEAPON_PICKUP_HPP

#include <string>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "Util/GameObject.hpp"

#include "data/GameData.hpp"

namespace Game {

/**
 * @class WeaponPickup
 * @brief A dropped weapon the player can walk over to equip.
 *
 * Holds a (non-owning) @ref WeaponDef pointer resolved from a loot drop. On
 * player contact the scene swaps the player's @c WeaponInstance to this def and
 * removes the pickup. Visual is a placeholder weapon-icon sprite.
 */
class WeaponPickup : public Util::GameObject {
public:
    WeaponPickup(const std::string &resourceRoot, glm::vec2 pos,
                 const WeaponDef *def);

    const WeaponDef *Def() const { return m_Def; }
    glm::vec2 Position() const { return m_Transform.translation; }
    Util::Collider GetCollider() const;

private:
    const WeaponDef *m_Def = nullptr;
};

} // namespace Game

#endif /* GAME_WEAPON_PICKUP_HPP */
