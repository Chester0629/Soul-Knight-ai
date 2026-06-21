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
 * Renders the picked hero's animation (project convention: frames 0-3 = walk,
 * 4-7 = idle), faces its movement direction, and moves with WASD at a speed
 * derived from the character definition. Carries its CombatStats. World space is
 * center-origin Cartesian (y up), matching the engine.
 */
class Player : public Util::GameObject {
public:
    /// @param charId "c01".."c13": selects the per-hero sprite set (cNN_0..7).
    Player(const CharacterDef &def, const std::string &resourceRoot,
           const std::string &charId);

    /// WASD movement + walk/idle animation + facing for this step.
    void Update(float dtMs) override;

    glm::vec2 Position() const { return m_Transform.translation; }
    const CombatStats &Stats() const { return m_Stats; }
    CombatStats &Stats() { return m_Stats; }

private:
    CombatStats m_Stats;
    float m_Speed; ///< pixels per second
    std::shared_ptr<Util::Animation> m_WalkAnim; ///< cNN_0..3
    std::shared_ptr<Util::Animation> m_IdleAnim; ///< cNN_4..7
    bool m_Moving = false;     ///< current clip state (avoid re-SetDrawable each frame).
    bool m_FacingLeft = false; ///< sprite flip (scale.x sign) by last horizontal move.
};
} // namespace Game

#endif /* GAME_PLAYER_HPP */
