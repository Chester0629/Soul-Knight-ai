// Standalone HUD bar render-capability check (Phase 2, Path A).
//
// This is NOT wired into Hud.cpp / GameScene -- it is an independent verification
// entry point. It boots a real PTSD context (window + GL), then proves three
// things with the ACTUAL engine, not by reasoning:
//
//   1. SIZE CONTRACT: a baked tinted PNG loaded as Util::Image reports
//      GetSize() == native 64x7 (== the JSON sprite_rect that Phase 1's
//      ScreenRectToPtsd divides by). If this breaks, the bar geometry is wrong.
//
//   2. FILL BEHAVIOUR (the centered-quad crack): PTSD scales the Image quad about
//      its CENTER. Rendering the hp bar at fraction 0.5 by scaling alone shrinks
//      it toward the centre (left edge moves in); FractionFillLeftAnchor's
//      compensation keeps the LEFT edge fixed and shrinks rightward (the Unity
//      [0,0.5]-pivot LtR fill). We render full / naive-half / compensated-half and
//      read the framebuffer back to MEASURE the left edges.
//
//   3. A screenshot (docs/hud/hud_bar_check.png) is saved for eyeballing colour +
//      fill direction.
//
// Build/run (opt-in; needs a desktop GL context):
//   cmake --build build --config Debug --target hud_bar_check
//   ./build/Debug/hud_bar_check
//
// Exit codes: 0 = all checks pass; 2 = size contract broken; 1 = fill check failed;
// 3 = no GL context (e.g. headless) -- the deterministic HudBarFill unit tests
// still cover the math in that case.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <SDL.h>
#include <SDL_image.h>
#include <glm/glm.hpp>

#include "Core/Context.hpp"
#include "Util/GameObject.hpp"
#include "Util/Image.hpp"
#include "Util/Transform.hpp"
#include "Util/TransformUtils.hpp"

#include "ui/HudLayout.hpp"

using Game::UI::FractionFillLeftAnchor;
using Game::UI::ScreenRect;
using Game::UI::ScreenRectToPtsd;

namespace {
constexpr int W = 1280;
constexpr int H = 720;
constexpr float kTexW = 64.0F;
constexpr float kTexH = 7.0F;

// Clear colour (dark blue); bar pixels are detected as "far" from this.
constexpr unsigned char kClearR = 10;
constexpr unsigned char kClearG = 10;
constexpr unsigned char kClearB = 30;

std::shared_ptr<Util::GameObject> MakeBar(const std::string &png,
                                          const Util::Transform &t) {
    auto go = std::make_shared<Util::GameObject>();
    go->SetDrawable(std::make_shared<Util::Image>(png));
    go->m_Transform = t;
    return go;
}

// Scan the framebuffer row at PTSD y == ty for lit (non-background) columns.
// glReadPixels is bottom-up; window row = 360 + ty. Returns [minX, maxX] in
// window pixels (left origin), or {-1,-1} if nothing lit.
std::pair<int, int> LitSpan(const std::vector<unsigned char> &px, float ty) {
    const int row = static_cast<int>(std::lround(360.0 + ty));
    if (row < 0 || row >= H) {
        return {-1, -1};
    }
    int minx = -1;
    int maxx = -1;
    for (int x = 0; x < W; ++x) {
        const std::size_t i = (static_cast<std::size_t>(row) * W + x) * 4;
        const int dr = px[i] - kClearR;
        const int dg = px[i + 1] - kClearG;
        const int db = px[i + 2] - kClearB;
        if (dr * dr + dg * dg + db * db > 40 * 40) {
            if (minx < 0) {
                minx = x;
            }
            maxx = x;
        }
    }
    return {minx, maxx};
}

void SaveScreenshot(const std::vector<unsigned char> &px, const std::string &path) {
    // Flip rows (GL is bottom-up; PNG is top-down).
    std::vector<unsigned char> flipped(static_cast<std::size_t>(W) * H * 4);
    for (int y = 0; y < H; ++y) {
        const unsigned char *src = &px[static_cast<std::size_t>(H - 1 - y) * W * 4];
        std::copy(src, src + static_cast<std::size_t>(W) * 4,
                  &flipped[static_cast<std::size_t>(y) * W * 4]);
    }
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(
        flipped.data(), W, H, 32, W * 4, SDL_PIXELFORMAT_ABGR8888);
    if (surf != nullptr) {
        if (IMG_SavePNG(surf, path.c_str()) == 0) {
            std::printf("  screenshot saved: %s\n", path.c_str());
        } else {
            std::printf("  WARN: IMG_SavePNG failed: %s\n", IMG_GetError());
        }
        SDL_FreeSurface(surf);
    }
}
} // namespace

int main(int /*argc*/, char ** /*argv*/) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered: survive a crash
    std::printf("hud_bar_check: booting context...\n");
    auto ctx = Core::Context::GetInstance();
    const GLubyte *ver = glGetString(GL_VERSION);
    if (ver == nullptr) {
        std::printf(
            "NO-GL: could not obtain an OpenGL context (headless?). The render "
            "check needs a desktop GL display. The HudBarFill unit tests cover "
            "the fill math deterministically without GL.\n");
        return 3;
    }
    std::printf("GL_VERSION: %s\n", reinterpret_cast<const char *>(ver));

    // The pixel readback geometry (glReadPixels W*H, window row = 360 + ty) assumes
    // a 1280x720 window. A config.json can override that (PTSD_Config::Init), which
    // would silently misalign the fill measurement -- guard against it.
    if (ctx->GetWindowWidth() != static_cast<unsigned int>(W) ||
        ctx->GetWindowHeight() != static_cast<unsigned int>(H)) {
        std::printf("ABORT: window is %ux%u but this check assumes %dx%d (config.json "
                    "override breaks the pixel readback).\n",
                    ctx->GetWindowWidth(), ctx->GetWindowHeight(), W, H);
        return 4;
    }

    const std::string root = RESOURCE_DIR;
    const std::string hpPng = root + "/sprites/ui_12_hp.png";
    const std::string armorPng = root + "/sprites/ui_12_armor.png";
    const std::string energyPng = root + "/sprites/ui_12_energy.png";

    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen space

    // ---- 1. SIZE CONTRACT via the REAL Util::Image::GetSize() ----
    std::printf("\n[1] size contract (Util::Image::GetSize() == native 64x7):\n");
    bool contractOk = true;
    for (const std::string &p : {hpPng, armorPng, energyPng}) {
        std::printf("    loading %s ...\n", p.c_str());
        Util::Image im(p);
        const glm::vec2 s = im.GetSize();
        const bool ok = static_cast<int>(s.x) == 64 && static_cast<int>(s.y) == 7;
        std::printf("    GetSize(%-18s) = (%g, %g)  %s\n",
                    p.substr(p.find_last_of('/') + 1).c_str(), s.x, s.y,
                    ok ? "OK" : "*** CONTRACT BROKEN ***");
        contractOk = contractOk && ok;
    }
    if (!contractOk) {
        std::printf("ABORT: size contract broken -- baked PNG != native sprite_rect.\n");
        return 2;
    }

    // ---- 2. build the hp full transform, then full / naive-0.5 / comp-0.5 ----
    const Util::Transform hpFull =
        ScreenRectToPtsd(ScreenRect{56.3F, 21.54F, 180.0F, 22.0F}, kTexW, kTexH);
    auto atY = [](Util::Transform t, float ty) {
        t.translation.y = ty;
        return t;
    };
    Util::Transform naive = hpFull;
    naive.scale.x *= 0.5F; // scale-only (the crack)
    const Util::Transform comp = FractionFillLeftAnchor(hpFull, 0.5F, kTexW);

    // Relocated to mid-screen rows purely so all variants fit one screenshot
    // (this exe verifies render capability, not on-screen position -- that is
    // Phase 1's job).
    const float yFull = 150.0F;
    const float yNaive = 80.0F;
    const float yComp = 10.0F;

    std::vector<std::shared_ptr<Util::GameObject>> objs;
    objs.push_back(MakeBar(hpPng, atY(hpFull, yFull)));
    objs.push_back(MakeBar(hpPng, atY(naive, yNaive)));
    objs.push_back(MakeBar(hpPng, atY(comp, yComp)));
    // colour artifact row: the three tints at full width
    objs.push_back(MakeBar(hpPng, atY(hpFull, -90.0F)));
    objs.push_back(MakeBar(armorPng, atY(hpFull, -140.0F)));
    objs.push_back(MakeBar(energyPng, atY(hpFull, -190.0F)));

    // ---- render one frame ----
    glViewport(0, 0, W, H);
    glClearColor(kClearR / 255.0F, kClearG / 255.0F, kClearB / 255.0F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (auto &o : objs) {
        o->Draw();
    }
    glFinish();

    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    SaveScreenshot(px, std::string(root) + "/../docs/hud/hud_bar_check.png");

    // ---- 3. measure left edges ----
    std::printf("\n[2] fill behaviour (measured framebuffer spans, window px):\n");
    const auto sFull = LitSpan(px, yFull);
    const auto sNaive = LitSpan(px, yNaive);
    const auto sComp = LitSpan(px, yComp);
    auto width = [](std::pair<int, int> s) { return s.first < 0 ? -1 : s.second - s.first; };
    std::printf("    full        : x=[%4d..%4d] w=%d   (expect left~56, w~180)\n",
                sFull.first, sFull.second, width(sFull));
    std::printf("    naive  x0.5 : x=[%4d..%4d] w=%d   (expect left~101, w~90 -> shrinks to CENTER)\n",
                sNaive.first, sNaive.second, width(sNaive));
    std::printf("    comp   x0.5 : x=[%4d..%4d] w=%d   (expect left~56,  w~90 -> LEFT edge fixed)\n",
                sComp.first, sComp.second, width(sComp));

    bool fillOk = true;
    if (sFull.first < 0 || sNaive.first < 0 || sComp.first < 0) {
        std::printf("    *** nothing rendered -- cannot verify fill ***\n");
        fillOk = false;
    } else {
        const bool compLeftFixed = std::abs(sComp.first - sFull.first) <= 3;
        const bool naiveShifted = (sNaive.first - sFull.first) >= 25;
        const bool compHalfWidth = std::abs(width(sComp) - width(sFull) / 2) <= 6;
        std::printf("    -> comp left edge fixed vs full   : %s (|d|=%d)\n",
                    compLeftFixed ? "YES" : "NO", std::abs(sComp.first - sFull.first));
        std::printf("    -> naive left edge shifted inward : %s (d=%d)  [the crack]\n",
                    naiveShifted ? "YES" : "NO", sNaive.first - sFull.first);
        std::printf("    -> comp width ~= half of full     : %s\n",
                    compHalfWidth ? "YES" : "NO");
        fillOk = compLeftFixed && naiveShifted && compHalfWidth;
    }

    std::printf("\nRESULT: size-contract=%s  fill-behaviour=%s\n",
                contractOk ? "PASS" : "FAIL", fillOk ? "PASS" : "FAIL");

    const int rc = (contractOk && fillOk) ? 0 : 1;
    // NOTE: PTSD destroys its static GL singletons (Image::s_Program / s_VertexArray)
    // AFTER the Context tears the GL context down, so normal static destruction calls
    // GL on a dead context and segfaults at exit. That is a pre-existing engine
    // lifetime quirk, unrelated to this check -- the verification and screenshot are
    // already done. Terminate immediately to skip the broken teardown.
    std::fflush(stdout);
    std::_Exit(rc);
}
