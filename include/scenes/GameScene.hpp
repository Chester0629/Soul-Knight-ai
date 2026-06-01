#ifndef GAME_GAME_SCENE_HPP
#define GAME_GAME_SCENE_HPP

#include <memory>

#include "Core/Camera2D.hpp"
#include "Core/Scene.hpp"

#include "Util/GameObject.hpp"
#include "Util/Renderer.hpp"

#include "data/GameData.hpp"
#include "entities/Player.hpp"

namespace Game {
/**
 * @class GameScene
 * @brief The in-game scene: a player you can walk around with a follow camera.
 *
 * First vertical-slice scene. Loads game data, spawns the player over a
 * background, renders through a Core::Camera2D that follows the player, and
 * forwards input-driven movement each frame. Combat, enemies, and HUD build on
 * top of this.
 */
class GameScene : public Core::Scene {
public:
    void OnEnter() override;
    void Update(float dtMs) override;
    void Render() override;

private:
    GameData m_Data;
    Core::Camera2D m_Camera;
    Util::Renderer m_Renderer;
    std::shared_ptr<Player> m_Player;
    std::shared_ptr<Util::GameObject> m_Background;
};
} // namespace Game

#endif /* GAME_GAME_SCENE_HPP */
