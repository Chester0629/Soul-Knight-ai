#ifndef GAME_HUD_HPP
#define GAME_HUD_HPP

#include "combat/CombatStats.hpp"

namespace Game {
/**
 * @class Hud
 * @brief Minimal ImGui combat overlay showing the player's vitals.
 *
 * Stateless, render-only helper: call @ref Draw once per frame from the scene's
 * update/draw pass. It does not own or mutate any combat state; it only reads a
 * @ref CombatStats snapshot and emits an ImGui window.
 */
class Hud {
public:
    /**
     * @brief Draw an ImGui overlay showing the player's HP / Armor / Energy.
     * @param player Snapshot of the player's combat vitals to display.
     */
    void Draw(const CombatStats &player);
};
} // namespace Game

#endif /* GAME_HUD_HPP */
