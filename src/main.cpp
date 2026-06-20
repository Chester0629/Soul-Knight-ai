#include <cstdlib> // std::getenv, std::atol
#include <cstring> // std::memcpy
#include <memory>
#include <vector>

#include <GL/glew.h>
#include <SDL_image.h>

#include "Core/Context.hpp"

#include "Util/Time.hpp"

#include "game/RunController.hpp"

namespace {
// Save the just-rendered back buffer to a PNG (GL is bottom-up; flip to top-down).
// Used only by the SK_SHOT test hook below; mirrors tools/hud_bar_check.cpp's saver.
void SaveFramePng(const char *path, int w, int h) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<unsigned char> flipped(px.size());
    const std::size_t stride = static_cast<std::size_t>(w) * 4;
    for (int y = 0; y < h; ++y) {
        std::memcpy(&flipped[static_cast<std::size_t>(y) * stride],
                    &px[static_cast<std::size_t>(h - 1 - y) * stride], stride);
    }
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
        flipped.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
    if (surf != nullptr) {
        IMG_SavePNG(surf, path);
        SDL_FreeSurface(surf);
    }
}
} // namespace

int main(int, char **) {
    auto context = Core::Context::GetInstance();

    // The RunController owns the SceneManager + RunState and puts floor 0 on the
    // stack (replacing the old single `scenes.Push(make_shared<GameScene>())`).
    Game::RunController run;
    run.StartRun();

    // --- Test / observability hooks (env-gated; NO effect unless set) ---
    // These do not change normal interactive play; they exist so the real game
    // binary can be smoke-tested, screenshotted, and driven to a *normal* exit
    // headlessly (which exercises the engine's true shutdown path).
    //   SK_MAX_FRAMES=N : run N frames, then request a clean exit (return 0).
    //   SK_SHOT=<path>  : save a PNG of the final rendered frame before exiting.
    const char *maxFramesEnv = std::getenv("SK_MAX_FRAMES");
    const long maxFrames = (maxFramesEnv != nullptr) ? std::atol(maxFramesEnv) : -1;
    const char *shotPath = std::getenv("SK_SHOT");
    long frame = 0;

    while (!context->GetExit()) {
        // Begins a new ImGui frame and pumps SDL events. Must be paired with
        // ImGui::Render() below before Context::Update() swaps the buffer.
        context->Setup();

        const float dtMs = Util::Time::GetDeltaTimeMs();
        run.Update(dtMs);
        run.Render();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Capture the completed frame from the back buffer *before* the swap.
        if (shotPath != nullptr && maxFrames > 0 && frame == maxFrames - 1) {
            SaveFramePng(shotPath, static_cast<int>(context->GetWindowWidth()),
                         static_cast<int>(context->GetWindowHeight()));
        }

        context->Update();

        if (maxFrames >= 0 && ++frame >= maxFrames) {
            context->SetExit(true); // request a clean exit -> normal teardown
        }
    }
    return 0;
}
