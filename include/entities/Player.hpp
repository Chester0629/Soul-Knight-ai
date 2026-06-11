#ifndef GAME_PLAYER_HPP
#define GAME_PLAYER_HPP

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Util/Animation.hpp"
#include "Util/GameObject.hpp"

#include "combat/CombatStats.hpp"
#include "data/GameData.hpp"

namespace Game {
/**
 * @class Player
 * @brief The player-controlled hero entity.
 *
 * Renders the c01 character animation and moves with WASD at a speed derived
 * from the character definition. Carries its CombatStats (hp/armor/energy).
 * World space is center-origin Cartesian (y up), matching the engine.
 */
class Player : public Util::GameObject {
public:
    Player(const CharacterDef &def, const std::string &resourceRoot);

    /// WASD movement for this step.
    void Update(float dtMs) override;

    glm::vec2 Position() const { return m_Transform.translation; }
    const CombatStats &Stats() const { return m_Stats; }
    CombatStats &Stats() { return m_Stats; }

private:
    CombatStats m_Stats;
    float m_Speed; ///< pixels per second
    std::shared_ptr<Util::Animation> m_Anim;
};
} // namespace Game

#endif /* GAME_PLAYER_HPP */
