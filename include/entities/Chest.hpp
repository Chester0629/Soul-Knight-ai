#ifndef GAME_CHEST_HPP
#define GAME_CHEST_HPP

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "Util/GameObject.hpp"
#include "Util/Image.hpp"

namespace Game {

/**
 * @class Chest
 * @brief An openable loot chest -- the port of the RGChest family
 *        (res/item/chest00..02, chest_room_reward, chest_boss<NN>): a closed
 *        sprite that swaps to its open sprite on the player's touch, a drop tier
 *        (RGChest.level -> LootTable::Roll) and, for boss chests, the guaranteed
 *        signature weapons (RGChestBoss.object_list) dropped beside the roll.
 *
 * FAITHFUL sprites (objects_common atlas): chest00 6 / chest01 3 / chest02 &
 * boss 13 closed, 0 / 0 / 10 open; chest_room_reward 9 closed, 8 open.
 */
class Chest : public Util::GameObject {
public:
    enum class Kind { Small, RoomReward, Boss };

    Chest(const std::string &resourceRoot, glm::vec2 pos, int tier, Kind kind = Kind::Small);

    int Tier() const { return m_Tier; }
    Kind GetKind() const { return m_Kind; }
    bool Opened() const { return m_Opened; }
    /// Open: swap to the open sprite. Returns false if already open.
    bool Open();
    /// RGChestBoss.object_list: weapons this chest always contains.
    void SetGuaranteed(std::vector<std::string> weaponIds) { m_Guaranteed = std::move(weaponIds); }
    const std::vector<std::string> &Guaranteed() const { return m_Guaranteed; }

    glm::vec2 Position() const { return m_Transform.translation; }
    Util::Collider GetCollider() const;

private:
    int m_Tier = 0;
    Kind m_Kind = Kind::Small;
    bool m_Opened = false;
    std::shared_ptr<Util::Image> m_OpenImage;
    std::vector<std::string> m_Guaranteed;
};

} // namespace Game

#endif /* GAME_CHEST_HPP */
