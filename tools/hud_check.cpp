// Standalone end-to-end check of the REAL Game::Hud (HUD landing Phase 3, a4).
//
// Unlike hud_bar_check (which tested the FractionFillLeftAnchor math in isolation),
// this drives the actual wired Game::Hud class: it loads Canvas.layout.json, builds
// the bg panel + 3 tinted bars + numbers exactly as GameScene does, and renders it
// at three vitals fractions on a clean background. It proves, against the engine:
//
//   1. The HUD builds + draws (no missing asset / no crash) through the real class.
//   2. The vitals bars are LEFT-ANCHORED: as cur/max drops full -> half -> quarter,
//      each bar's LEFT edge stays fixed while it shrinks rightward (the Soul-Knight
//      pivot [0,0.5] fill). Measured by reading the HP-bar row back from the
//      framebuffer at each fraction.
//   3. Screenshots (docs/hud/hud_check_{full,half,quarter}.png) for eyeballing
//      position / colour / numbers / shrink direction.
//
// Build/run (opt-in; needs a desktop GL display):
//   cmake --build build --config Debug --target hud_check
//   ./build/Debug/hud_check
//
// Exit codes: 0 = pass; 1 = left-anchor / width check failed; 3 = no GL context.
// (Uses std::_Exit like hud_bar_check: this tool's binary hits the link-order
// static-teardown segfault that the normal game does NOT -- see the HUD exit-segfault
// memory. The work + screenshots are done before exit.)

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include <GL/glew.h>
#include <SDL.h>
#include <SDL_image.h>
#include <glm/glm.hpp>

#include "Core/Context.hpp"
#include "Util/TransformUtils.hpp"

#include "combat/CombatStats.hpp"
#include "ui/Hud.hpp"

namespace {
constexpr int W = 1280;
constexpr int H = 720;

// HP bar fill is the baked ui_12_hp.png (~222,59,59). Detect "bar red": high R,
// low G/B. The bar sits on the HP row; the heart icon (baked into the ui_15 panel)
// is also reddish but to the LEFT of the bar slot, so we scan from x >= 50 and take
// the longest contiguous red run = the bar fill.
bool IsBarRed(const unsigned char *p) {
    return p[0] > 150 && p[1] < 110 && p[2] < 110;
}

void SaveShot(const std::vector<unsigned char> &px, const char *path) {
    std::vector<unsigned char> flip(static_cast<std::size_t>(W) * H * 4);
    const std::size_t stride = static_cast<std::size_t>(W) * 4;
    for (int y = 0; y < H; ++y) {
        std::copy(&px[static_cast<std::size_t>(H - 1 - y) * stride],
                  &px[static_cast<std::size_t>(H - 1 - y) * stride] + stride,
                  &flip[static_cast<std::size_t>(y) * stride]);
    }
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(flip.data(), W, H, 32,
                                                        W * 4, SDL_PIXELFORMAT_ABGR8888);
    if (s != nullptr) {
        IMG_SavePNG(s, path);
        SDL_FreeSurface(s);
    }
}

// Bar-red EXTENT (min..max x) on the HP-bar window row, scanning from x>=50 (skips
// the heart icon baked into the panel). The white number sits INSIDE the bar, so it
// only creates an interior gap -- min/max red still mark the true bar edges. Returns
// {leftX, width}; width = maxX - minX.
struct Span {
    int left = -1;
    int width = -1;
};
Span HpBarSpan(const std::vector<unsigned char> &px, int row) {
    int minx = -1;
    int maxx = -1;
    for (int x = 50; x < W; ++x) {
        const std::size_t i = (static_cast<std::size_t>(row) * W + x) * 4;
        if (IsBarRed(&px[i])) {
            if (minx < 0) {
                minx = x;
            }
            maxx = x;
        }
    }
    Span s;
    s.left = minx;
    s.width = (minx < 0) ? -1 : (maxx - minx);
    return s;
}

Game::CombatStats Stats(int frac /*1=full,2=half,4=quarter denom*/) {
    Game::CombatStats s;
    s.maxHp = 100;
    s.hp = 100 / frac;
    s.maxArmor = 50;
    s.armor = 50 / frac;
    s.maxEnergy = 200;
    s.energy = 200 / frac;
    return s;
}
} // namespace

int main(int /*argc*/, char ** /*argv*/) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("hud_check: booting context...\n");
    auto ctx = Core::Context::GetInstance();
    if (glGetString(GL_VERSION) == nullptr) {
        std::printf("NO-GL: no OpenGL context (headless?). Skipping render check.\n");
        return 3;
    }
    std::printf("GL_VERSION: %s\n",
                reinterpret_cast<const char *>(glGetString(GL_VERSION)));

    // The hardcoded W/H + hpRow (360+327) and the glViewport assume a 1280x720
    // back buffer. Sprite placement uses PTSD_Config window size, which a
    // config.json could override -- that would silently misalign the readback row.
    if (ctx->GetWindowWidth() != static_cast<unsigned int>(W) ||
        ctx->GetWindowHeight() != static_cast<unsigned int>(H)) {
        std::printf("ABORT: window is %ux%u but this check assumes %dx%d.\n",
                    ctx->GetWindowWidth(), ctx->GetWindowHeight(), W, H);
        std::fflush(stdout);
        std::_Exit(4);
    }

    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen space, as GameScene does
    Game::Hud hud;

    const char *names[] = {"full", "half", "quarter"};
    const int fracs[] = {1, 2, 4};
    const char *paths[] = {"/../docs/hud/hud_check_full.png",
                           "/../docs/hud/hud_check_half.png",
                           "/../docs/hud/hud_check_quarter.png"};
    const std::string root = RESOURCE_DIR;

    // The HP bar's window row: its PTSD center y is ~327.46 (top of screen);
    // glReadPixels is bottom-up so window row = 360 + ty.
    const int hpRow = 360 + 327;

    Span spans[3];
    for (int k = 0; k < 3; ++k) {
        glViewport(0, 0, W, H);
        glClearColor(10 / 255.0F, 10 / 255.0F, 30 / 255.0F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        hud.Draw(Stats(fracs[k])); // first call lazily builds; later calls update
        glFinish();

        std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        SaveShot(px, (root + paths[k]).c_str());
        spans[k] = HpBarSpan(px, hpRow);
        std::printf("  %-8s hp=%d/100 -> HP bar: leftX=%d width=%d\n", names[k],
                    100 / fracs[k], spans[k].left, spans[k].width);
    }

    // Left-anchored fill: LEFT edge fixed across all fractions; width ~ fraction.
    bool ok = spans[0].left >= 0 && spans[1].left >= 0 && spans[2].left >= 0;
    if (ok) {
        const bool leftFixed = std::abs(spans[1].left - spans[0].left) <= 3 &&
                               std::abs(spans[2].left - spans[0].left) <= 3;
        const bool halfWidth =
            std::abs(spans[1].width - spans[0].width / 2) <= 6;
        const bool quarterWidth =
            std::abs(spans[2].width - spans[0].width / 4) <= 6;
        std::printf("\n  -> left edge fixed (full|half|quarter): %s\n",
                    leftFixed ? "YES" : "NO");
        std::printf("  -> half  width ~= 1/2 full: %s\n", halfWidth ? "YES" : "NO");
        std::printf("  -> quarter width ~= 1/4 full: %s\n",
                    quarterWidth ? "YES" : "NO");
        ok = leftFixed && halfWidth && quarterWidth;
    } else {
        std::printf("\n  *** HP bar not detected -- cannot verify ***\n");
    }
    std::printf("\nRESULT: %s\n", ok ? "PASS" : "FAIL");

    std::fflush(stdout);
    std::_Exit(ok ? 0 : 1); // skip the tool-binary static-teardown segfault
}
