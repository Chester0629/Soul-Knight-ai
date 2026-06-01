#include <memory>

#include "Core/Context.hpp"
#include "Core/SceneManager.hpp"

#include "Util/Time.hpp"

#include "scenes/GameScene.hpp"

int main(int, char**) {
    auto context = Core::Context::GetInstance();

    Core::SceneManager scenes;
    scenes.Push(std::make_shared<Game::GameScene>());

    while (!context->GetExit()) {
        // Begins a new ImGui frame and pumps SDL events. Must be paired with
        // ImGui::Render() below before Context::Update() swaps the buffer.
        context->Setup();

        const float dtMs = Util::Time::GetDeltaTimeMs();
        scenes.Update(dtMs);
        scenes.Render();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        context->Update();
    }
    return 0;
}
