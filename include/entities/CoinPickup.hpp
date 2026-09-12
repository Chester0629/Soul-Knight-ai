#ifndef GAME_COIN_PICKUP_HPP
#define GAME_COIN_PICKUP_HPP

#include <string>

#include <glm/glm.hpp>

#include "Util/GameObject.hpp"

#include "game/CoinMagnet.hpp"
#include "data/GameData.hpp"
#include "game/EnemyReward.hpp"

namespace Game {

/**
 * @class CoinPickup
 * @brief A dropped coin / energy orb (B3 economy) -- the view + motion of the
 *        original's coin_0 / coin_1 / coin_2 / energy register prefabs
 *        (RGCoin / RGEnergy). Spins through the prefab's REAL 4-frame clip
 *        (coin_0.controller: coin_gold / coin_silver / coin_copper, objects_common
 *        atlas; energy: bullet_16 orb), scatters on spawn, then homes on the
 *        player inside its magnet range (@ref CoinMagnet) and is collected on
 *        contact -- coins add to RGGameProcess.coin_value (RunStats::coins),
 *        energy refills the player's energy (RGEnergy.GetItem).
 */
class CoinPickup : public Util::GameObject {
public:
    /// @p def is the kind's pickups.json entry (value / speed / range / frames);
    /// @p friction RGCoin.friction from the same table; @p scatterImpulse T-19.
    CoinPickup(const std::string &resourceRoot, glm::vec2 pos, RewardKind kind,
               glm::vec2 scatterDir, const PickupDef &def, float friction, float scatterImpulse);

    RewardKind Kind() const { return m_Kind; }
    int Value() const { return m_Value; }
    glm::vec2 Position() const { return m_Transform.translation; }

    /// Latch homing regardless of range (floor clear: nothing is left behind).
    void Attract() { m_Magnet.findTarget = true; }

    /// Advance the scatter / magnet motion; @return true when collected.
    bool Update(float dtMs, glm::vec2 playerPos);

private:
    RewardKind m_Kind;
    int m_Value;
    CoinMagnet m_Magnet;
};

} // namespace Game

#endif /* GAME_COIN_PICKUP_HPP */
