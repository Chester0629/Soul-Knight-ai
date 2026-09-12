#include <cstdlib> // std::getenv, std::atol
#include <cstring> // std::memcpy
#include <memory>
#include <string>
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
    // Read explicitly from the DEFAULT framebuffer's back buffer: the ImGui GL3
    // backend / the renderer can leave another FBO or read buffer bound, which made
    // the previous capture read an empty target. Finish pending draws first.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glFinish();
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
    // SK_SEED=N test hook: run seed (default RunState::kDefaultRunSeed) so a
    // headless proof can pick a chapter variant / boss room deterministically.
    const char *seedEnv = std::getenv("SK_SEED");
    Game::RunController run(seedEnv != nullptr ? std::atoi(seedEnv) : Game::RunState::kDefaultRunSeed);
    // Entry flow (B6/B5): main menu -> hero select -> run. Headless hooks (env,
    // NO-OP unless set): SK_START_CHAR=cNN starts a run as that hero directly;
    // SK_SKIP_MENU=1 starts as the default hero -- both skip the two screens so
    // smoke runs / log proofs land straight in gameplay.
    if (const char *hero = std::getenv("SK_START_CHAR"); hero != nullptr && *hero != '\0') {
        run.StartRun(std::string(hero));
    } else if (std::getenv("SK_SKIP_MENU") != nullptr) {
        run.StartRun();
    } else {
        run.ShowMenu();
    }

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

        // Capture the shot frame. Reading the default framebuffer of a window that
        // is covered / unfocused fails the GL pixel-ownership test on Windows and
        // yields an all-black image (the "blank SK_SHOT" symptom), so re-render the
        // scene into an offscreen FBO -- which always owns its pixels -- and read that.
        if (shotPath != nullptr && maxFrames > 0 && frame == maxFrames - 1) {
            const int w = static_cast<int>(context->GetWindowWidth());
            const int h = static_cast<int>(context->GetWindowHeight());
            GLuint fbo = 0;
            GLuint tex = 0;
            glGenFramebuffers(1, &fbo);
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glViewport(0, 0, w, h);
                glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
                glClear(GL_COLOR_BUFFER_BIT);
                run.Render(); // second render of the same frame, into the FBO
                glFinish();
                std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                std::vector<unsigned char> flipped(px.size());
                const std::size_t stride = static_cast<std::size_t>(w) * 4;
                for (int y = 0; y < h; ++y) {
                    std::memcpy(&flipped[static_cast<std::size_t>(y) * stride],
                                &px[static_cast<std::size_t>(h - 1 - y) * stride], stride);
                }
                SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
                    flipped.data(), w, h, 32, static_cast<int>(stride), SDL_PIXELFORMAT_RGBA32);
                if (surf != nullptr) {
                    IMG_SavePNG(surf, shotPath);
                    SDL_FreeSurface(surf);
                }
            } else {
                SaveFramePng(shotPath, w, h); // fallback: the old back-buffer read
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
        }

        context->Update();

        if (maxFrames >= 0 && ++frame >= maxFrames) {
            context->SetExit(true); // request a clean exit -> normal teardown
        }
    }
    return 0;
}
