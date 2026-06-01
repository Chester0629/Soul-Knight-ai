#include "scenes/GameScene.hpp"

#include <string>

#include <glm/glm.hpp>

#include "Core/Context.hpp"

#include "Util/Image.hpp"
#include "Util/Input.hpp"
#include "Util/Keycode.hpp"
#include "Util/Logger.hpp"
#include "Util/TransformUtils.hpp"

namespace Game {

void GameScene::OnEnter() {
    m_Data.LoadAll(RESOURCE_DIR);

    const std::string root = RESOURCE_DIR;

    // Background at the back (z = 0).
    m_Background = std::make_shared<Util::GameObject>();
    m_Background->SetDrawable(
        std::make_shared<Util::Image>(root + "/sprites/Background.png"));
    m_Background->SetZIndex(0.0F);

    // Player on top (z = 5).
    m_Player = std::make_shared<Player>(m_Data.PlayerTemplate(), root);

    m_Renderer.AddChild(m_Background);
    m_Renderer.AddChild(m_Player);

    m_Camera.SetPosition(m_Player->Position());
    LOG_INFO("GameScene entered: player + background loaded");
}

void GameScene::Update(float dtMs) {
    if (Util::Input::IsKeyUp(Util::Keycode::ESCAPE) || Util::Input::IfExit()) {
        Core::Context::GetInstance()->SetExit(true);
        return;
    }

    m_Player->Update(dtMs);
    m_Camera.Follow(m_Player->Position(), 0.1F); // smooth follow
    m_Camera.Update(dtMs);
}

void GameScene::Render() {
    Util::SetActiveViewMatrix(m_Camera.GetViewMatrix());
    m_Renderer.Update();
    // Reset so any screen-space UI drawn afterwards isn't moved by the camera.
    Util::SetActiveViewMatrix(glm::mat4(1.0F));
}

} // namespace Game
