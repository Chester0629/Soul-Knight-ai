#ifndef GAME_CHEST_HPP
#define GAME_CHEST_HPP

#include <string>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "Util/GameObject.hpp"

namespace Game {

/**
 * @class Chest
 * @brief An openable loot chest placed in a room.
 *
 * Carries a @ref Tier (chest_level) used by @c LootTable::Roll. The scene opens
 * it on player proximity (each chest opens once), rolls a drop, and spawns a
 * @c WeaponPickup. Visual is a placeholder box sprite until a dedicated chest
 * sprite is wired (report #2 polish).
 */
class Chest : public Util::GameObject {
public:
    Chest(const std::string &resourceRoot, glm::vec2 pos, int tier);

    int Tier() const { return m_Tier; }
    bool Opened() const { return m_Opened; }
    void Open() { m_Opened = true; }

    glm::vec2 Position() const { return m_Transform.translation; }
    Util::Collider GetCollider() const;

private:
    int m_Tier = 0;
    bool m_Opened = false;
};

} // namespace Game

#endif /* GAME_CHEST_HPP */
