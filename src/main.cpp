#include "App.hpp"

#include "Core/Context.hpp"

int main(int, char**) {
    auto context = Core::Context::GetInstance();
    App app;

    while (!context->GetExit()) {
        // Begins a new ImGui frame and pumps SDL events. Must be paired with
        // ImGui::Render() below before Context::Update() swaps the buffer,
        // otherwise ImGui asserts on the next NewFrame().
        context->Setup();

        switch (app.GetCurrentState()) {
            case App::State::START:
                app.Start();
                break;

            case App::State::UPDATE:
                app.Update();
                break;

            case App::State::END:
                app.End();
                context->SetExit(true);
                break;
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        context->Update();
    }
    return 0;
}
