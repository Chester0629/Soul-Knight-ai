#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "ui/HudLayout.hpp"

using Game::UI::Classify;
using Game::UI::ClassifyNode;
using Game::UI::Extent;
using Game::UI::FractionFillLeftAnchor;
using Game::UI::RenderedSpanX;
using Game::UI::SpanX;
using Game::UI::LayoutDoc;
using Game::UI::LayoutNode;
using Game::UI::Parked;
using Game::UI::ParkedDelta;
using Game::UI::ScreenRect;
using Game::UI::ScreenRectToPtsd;
using Game::UI::ShiftRect;
using Game::UI::Vec2;

// NOLINTBEGIN(readability-magic-numbers)

// Faithful excerpt of docs/layout/Canvas.layout.json: the parked HUD `state_bar`
// subtree (bg + 3 vitals bars, each with its `img` fill), plus the on-screen
// `control` (straddles bottom) and the bottom-left `btn_home` (fully on-screen).
// Values are copied verbatim from the extractor's baked fields. (Backslashes are
// doubled because this is a raw literal carrying JSON-escaped paths.)
static const char *kFixture = R"json(
{
  "coordinate_space": "screen",
  "canvas": { "w": 1280, "h": 720 },
  "roots": [
    {
      "name": "Canvas",
      "screen_rect": { "left": 0.0, "top": 0.0, "w": 1280.0, "h": 720.0 },
      "children": [
        {
          "name": "state_bar",
          "screen_rect": { "left": 30.0, "top": -100.0, "w": 0.0, "h": 0.0 },
          "children": [
            {
              "name": "bg",
              "screen_rect": { "left": 30.0, "top": -142.94, "w": 246.88, "h": 121.88 },
              "visual": [ {
                "kind": "Image",
                "sprite": { "name": "ui_15", "sprite_rect": {"x":110.0,"y":395.0,"w":79.0,"h":39.0}, "png": "docs\\layout\\crops\\ui_15.png" },
                "color": {"r":1.0,"g":1.0,"b":1.0,"a":1.0}, "image_type": 0
              } ]
            },
            {
              "name": "hp_bar",
              "screen_rect": { "left": 75.3, "top": -133.9, "w": 210.0, "h": 35.0 },
              "children": [ {
                "name": "img",
                "screen_rect": { "left": 80.3, "top": -127.4, "w": 180.0, "h": 22.0 },
                "visual": [ {
                  "kind": "Image",
                  "sprite": { "name": "ui_12", "sprite_rect": {"x":512.0,"y":374.0,"w":64.0,"h":7.0}, "png": "docs\\layout\\crops\\ui_12.png" },
                  "color": {"r":0.86764705,"g":0.22967128,"b":0.22967128,"a":1.0}, "image_type": 0
                } ]
              } ]
            },
            {
              "name": "armor_bar",
              "screen_rect": { "left": 75.3, "top": -102.5, "w": 210.0, "h": 35.0 },
              "children": [ {
                "name": "img",
                "screen_rect": { "left": 80.3, "top": -96.0, "w": 180.0, "h": 22.0 },
                "visual": [ {
                  "kind": "Image",
                  "sprite": { "name": "ui_12", "sprite_rect": {"x":512.0,"y":374.0,"w":64.0,"h":7.0}, "png": "docs\\layout\\crops\\ui_12.png" },
                  "color": {"r":0.5514706,"g":0.5514706,"b":0.5514706,"a":1.0}, "image_type": 0
                } ]
              } ]
            },
            {
              "name": "energy_bar",
              "screen_rect": { "left": 75.3, "top": -71.1, "w": 210.0, "h": 35.0 },
              "children": [ {
                "name": "img",
                "screen_rect": { "left": 80.3, "top": -64.6, "w": 180.0, "h": 22.0 },
                "visual": [ {
                  "kind": "Image",
                  "sprite": { "name": "ui_12", "sprite_rect": {"x":512.0,"y":374.0,"w":64.0,"h":7.0}, "png": "docs\\layout\\crops\\ui_12.png" },
                  "color": {"r":0.124891855,"g":0.44847536,"b":0.77205884,"a":1.0}, "image_type": 0
                } ]
              } ]
            }
          ]
        },
        {
          "name": "control",
          "screen_rect": { "left": 0.0, "top": 400.0, "w": 1280.0, "h": 720.0 }
        },
        {
          "name": "btn_home",
          "screen_rect": { "left": 60.0, "top": 570.0, "w": 120.0, "h": 100.0 }
        }
      ]
    }
  ]
}
)json";

// =====================================================================
// 1. Converter -- the screen_rect -> PTSD Y-flip, against the verified
//    working-project ground truth (the ui_15 panel at on-screen 6px inset).
// =====================================================================

TEST(HudLayoutConverter, PanelGroundTruth) {
    // bg rect (6,6,237,117) with ui_15 native 79x39.
    const auto t = ScreenRectToPtsd(ScreenRect{6.0F, 6.0F, 237.0F, 117.0F}, 79.0F, 39.0F);
    // External ground truth (independent derivation): the sibling working project
    // (D:/Soul Knight/project, include/UI/HUD.hpp) lands this same ui_15 panel at
    // PANEL_CX=-515.5, PANEL_CY=295.5 via a corner-anchor path, NOT this formula --
    // so matching it cross-validates the flip math. The sprite is drawn CENTERED at
    // translation, so this is the panel center.
    EXPECT_NEAR(t.translation.x, -515.5F, 1e-3F);
    EXPECT_NEAR(t.translation.y, 295.5F, 1e-3F);
    // 237/79 = 3.0 ; 117/39 = 3.0
    EXPECT_NEAR(t.scale.x, 3.0F, 1e-4F);
    EXPECT_NEAR(t.scale.y, 3.0F, 1e-4F);
}

TEST(HudLayoutConverter, CenterOfCanvasMapsToOrigin) {
    // A rect centered on the 1280x720 canvas must land at PTSD (0,0).
    const auto t = ScreenRectToPtsd(ScreenRect{540.0F, 260.0F, 200.0F, 200.0F}, 0.0F, 0.0F);
    EXPECT_NEAR(t.translation.x, 0.0F, 1e-4F);
    EXPECT_NEAR(t.translation.y, 0.0F, 1e-4F);
    EXPECT_FLOAT_EQ(t.scale.x, 1.0F); // texW=0 -> scale defaults to 1
    EXPECT_FLOAT_EQ(t.scale.y, 1.0F);
}

// =====================================================================
// 2. Loader -- read the baked state_bar subtree fields verbatim.
// =====================================================================

TEST(HudLayoutLoader, ReadsStateBarSubtree) {
    const LayoutDoc doc = LayoutDoc::ParseString(kFixture);
    ASSERT_TRUE(doc.ok());
    EXPECT_FLOAT_EQ(doc.canvasW(), 1280.0F);
    EXPECT_FLOAT_EQ(doc.canvasH(), 720.0F);

    const LayoutNode *bg = doc.Find("state_bar/bg");
    ASSERT_NE(bg, nullptr);
    ASSERT_EQ(bg->visual.size(), 1U);
    EXPECT_EQ(bg->visual[0].spriteName, "ui_15");
    EXPECT_EQ(bg->visual[0].spritePng, "docs\\layout\\crops\\ui_15.png");
    EXPECT_NEAR(bg->rect.left, 30.0F, 1e-3F);
    EXPECT_NEAR(bg->rect.top, -142.94F, 1e-3F);

    struct Bar {
        const char *path;
        float r, g, b;
    };
    const Bar bars[] = {
        {"state_bar/hp_bar/img", 0.86764705F, 0.22967128F, 0.22967128F},
        {"state_bar/armor_bar/img", 0.5514706F, 0.5514706F, 0.5514706F},
        {"state_bar/energy_bar/img", 0.124891855F, 0.44847536F, 0.77205884F},
    };
    for (const auto &b : bars) {
        const LayoutNode *n = doc.Find(b.path);
        ASSERT_NE(n, nullptr) << b.path;
        // baked rect: all three fills are 180x22, left 80.3
        EXPECT_NEAR(n->rect.left, 80.3F, 1e-3F) << b.path;
        EXPECT_NEAR(n->rect.w, 180.0F, 1e-3F) << b.path;
        EXPECT_NEAR(n->rect.h, 22.0F, 1e-3F) << b.path;
        ASSERT_EQ(n->visual.size(), 1U) << b.path;
        const auto &v = n->visual[0];
        EXPECT_EQ(v.kind, "Image") << b.path;
        EXPECT_EQ(v.spriteName, "ui_12") << b.path;            // one shared sprite
        EXPECT_EQ(v.spritePng, "docs\\layout\\crops\\ui_12.png") << b.path;
        EXPECT_NEAR(v.texW, 64.0F, 1e-3F) << b.path;           // ui_12 native 64x7
        EXPECT_NEAR(v.texH, 7.0F, 1e-3F) << b.path;
        EXPECT_EQ(v.imageType, 0) << b.path;
        EXPECT_NEAR(v.tint.r, b.r, 1e-5F) << b.path;           // per-bar tint
        EXPECT_NEAR(v.tint.g, b.g, 1e-5F) << b.path;
        EXPECT_NEAR(v.tint.b, b.b, 1e-5F) << b.path;
        EXPECT_NEAR(v.tint.a, 1.0F, 1e-5F) << b.path;
    }

    EXPECT_EQ(doc.Find("state_bar/nope"), nullptr);
    EXPECT_EQ(doc.Find("does_not_exist"), nullptr);
}

TEST(HudLayoutLoader, RejectsNonScreenSpaceDoc) {
    // Prefab-local layout files bake `local_rect`, not `screen_rect`; this
    // screen-space module must reject them rather than silently read 0x0 geometry.
    const char *prefabLocal = R"json(
    { "coordinate_space": "prefab_local", "canvas": {"w":1280,"h":720},
      "roots": [ { "name": "Root", "local_rect": {"left":0,"top":0,"w":100,"h":100} } ] }
    )json";
    const LayoutDoc doc = LayoutDoc::ParseString(prefabLocal);
    EXPECT_FALSE(doc.ok());
    EXPECT_EQ(doc.Find("Root"), nullptr);
}

TEST(HudLayoutLoader, MalformedInputIsNotOk) {
    EXPECT_FALSE(LayoutDoc::ParseString("{ not valid json ").ok());
    EXPECT_FALSE(LayoutDoc::ParseString("[]").ok());          // not an object
    EXPECT_FALSE(LayoutDoc::Load("D:/no/such/file.layout.json").ok());
}

// =====================================================================
// 3a. Parked classification -- pure classifier on the design's verified
//     ground-truth bboxes (HUD_BUILD_PLAN.md s2.6a table).
// =====================================================================

TEST(HudLayoutParked, ClassifyDesignTable) {
    // (L,T,R,B) and expected verdict, copied from the verified plan table.
    EXPECT_EQ(Classify(Extent{30.0F, -143.0F, 381.0F, -21.0F}), Parked::Parked)
        << "state_bar (fully above)";
    EXPECT_EQ(Classify(Extent{992.0F, -130.0F, 1300.0F, -30.0F}), Parked::Parked)
        << "text_gems";
    EXPECT_EQ(Classify(Extent{821.0F, -150.0F, 1270.0F, -20.0F}), Parked::Parked)
        << "info_bar";
    EXPECT_EQ(Classify(Extent{330.0F, 800.0F, 950.0F, 930.0F}), Parked::Parked)
        << "setting_bar (fully below)";
    EXPECT_EQ(Classify(Extent{60.0F, 570.0F, 180.0F, 670.0F}), Parked::Normal)
        << "btn_home (on-screen bottom-left) -- must NOT be mismarked";
    EXPECT_EQ(Classify(Extent{0.0F, 400.0F, 1280.0F, 1120.0F}), Parked::Ambiguous)
        << "control (virtual joystick straddles the bottom)";
}

// =====================================================================
// 3a'. Parked classification -- end to end through the loader on real
//      (fixture) nodes: 0x0-container union-bbox path + own-rect path.
// =====================================================================

TEST(HudLayoutParked, ClassifyLoadedNodes) {
    const LayoutDoc doc = LayoutDoc::ParseString(kFixture);
    ASSERT_TRUE(doc.ok());

    // state_bar is a 0x0 container -> classified by the union bbox of descendants.
    const LayoutNode *stateBar = doc.Find("state_bar");
    ASSERT_NE(stateBar, nullptr);
    const Extent ext = Game::UI::RenderedExtent(*stateBar);
    EXPECT_NEAR(ext.t, -142.94F, 1e-2F); // bg top is the highest point
    EXPECT_LE(ext.b, 0.0F);              // bottom-most edge still above the screen
    EXPECT_EQ(ClassifyNode(*stateBar), Parked::Parked);

    const LayoutNode *control = doc.Find("control");
    ASSERT_NE(control, nullptr);
    EXPECT_EQ(ClassifyNode(*control), Parked::Ambiguous);

    const LayoutNode *btnHome = doc.Find("btn_home");
    ASSERT_NE(btnHome, nullptr);
    EXPECT_EQ(ClassifyNode(*btnHome), Parked::Normal);
}

// =====================================================================
// 3b. Anchor override -- per-element ABSOLUTE PTSD coords (no container move).
//     Anchor `bg` to the on-screen top-left (6,6); rigid-shift each bar's own
//     rect; convert -> the verified hand-calc coordinates.
// =====================================================================

TEST(HudLayoutParked, AnchorOverrideBars) {
    const LayoutDoc doc = LayoutDoc::ParseString(kFixture);
    ASSERT_TRUE(doc.ok());

    const LayoutNode *bg = doc.Find("state_bar/bg");
    ASSERT_NE(bg, nullptr);
    // Client supplies the known on-screen anchor for the reference child (bg).
    const Vec2 delta = ParkedDelta(bg->rect, Vec2{6.0F, 6.0F});
    EXPECT_NEAR(delta.x, -24.0F, 1e-2F);     // 6 - 30
    EXPECT_NEAR(delta.y, 148.94F, 1e-2F);    // 6 - (-142.94)

    struct Expect {
        const char *path;
        float ptsdX, ptsdY;
    };
    // Hand-calc ground truth (HUD_BUILD_PLAN.md s2.6b), exact converter output:
    //   shifted.top = static.top + 148.94 ; ty = 360 - (shifted.top + 11)
    const Expect ex[] = {
        {"state_bar/hp_bar/img", -493.7F, 327.46F},     // top -127.4 -> 21.54
        {"state_bar/armor_bar/img", -493.7F, 296.06F},  // top  -96.0 -> 52.94
        {"state_bar/energy_bar/img", -493.7F, 264.66F}, // top  -64.6 -> 84.34
    };
    float prevY = 0.0F;
    for (int i = 0; i < 3; ++i) {
        const LayoutNode *n = doc.Find(ex[i].path);
        ASSERT_NE(n, nullptr) << ex[i].path;
        ASSERT_EQ(n->visual.size(), 1U) << ex[i].path;
        const ScreenRect shifted = ShiftRect(n->rect, delta);
        const auto t = ScreenRectToPtsd(shifted, n->visual[0].texW, n->visual[0].texH);

        EXPECT_NEAR(t.translation.x, ex[i].ptsdX, 1e-2F) << ex[i].path;
        EXPECT_NEAR(t.translation.y, ex[i].ptsdY, 1e-2F) << ex[i].path;
        // The task's headline check: hp_bar/img ~= (-493.7, 327.4) within +/-0.5.
        if (i == 0) {
            EXPECT_NEAR(t.translation.x, -493.7F, 0.5F);
            EXPECT_NEAR(t.translation.y, 327.4F, 0.5F);
        }
        // All bars must be fully in-bounds after the shift.
        EXPECT_GE(t.translation.x, -640.0F) << ex[i].path;
        EXPECT_LE(t.translation.x, 640.0F) << ex[i].path;
        EXPECT_GE(t.translation.y, -360.0F) << ex[i].path;
        EXPECT_LE(t.translation.y, 360.0F) << ex[i].path;
        // ui_12 fill scale: 180/64 wide, 22/7 tall.
        EXPECT_NEAR(t.scale.x, 180.0F / 64.0F, 1e-4F) << ex[i].path;
        EXPECT_NEAR(t.scale.y, 22.0F / 7.0F, 1e-4F) << ex[i].path;
        // ~31px vertical spacing is preserved by the rigid shift.
        if (i > 0) {
            EXPECT_NEAR(prevY - t.translation.y, 31.4F, 0.1F) << ex[i].path;
        }
        prevY = t.translation.y;
    }
}

// =====================================================================
// 3d. Fractional fill (Path-A render seam) -- the centered-quad crack and
//     the left-anchor compensation, proven against the engine's quad math.
// =====================================================================

namespace {
// The hp bar's FULL transform after the parked anchor (Phase 1 result): slot
// (56.3, 21.54, 180, 22), ui_12 native 64x7. Built through the real converter so
// the test is grounded in the actual pipeline, not a hand-typed transform.
constexpr float kTexW = 64.0F;
Util::Transform HpFullTransform() {
    return ScreenRectToPtsd(ScreenRect{56.3F, 21.54F, 180.0F, 22.0F}, kTexW, 7.0F);
}
} // namespace

TEST(HudBarFill, CenteredQuadScaleShrinksAboutCenter_TheCrack) {
    // Naive "just multiply scale.x by f" -- what Path A would do without thinking.
    const Util::Transform full = HpFullTransform();
    const SpanX fullSpan = RenderedSpanX(full, kTexW);
    EXPECT_NEAR(fullSpan.left, -583.7F, 1e-2F);   // window px 56.3 from left
    EXPECT_NEAR(fullSpan.right, -403.7F, 1e-2F);
    EXPECT_NEAR(fullSpan.right - fullSpan.left, 180.0F, 1e-2F);

    Util::Transform naive = full;
    naive.scale.x = full.scale.x * 0.5F; // scale only, no translate
    const SpanX naiveSpan = RenderedSpanX(naive, kTexW);
    // The bug: left edge moves INWARD by (1-f)*w/2 = 45px; center stays put.
    EXPECT_NEAR(naiveSpan.left, -538.7F, 1e-2F);  // moved right by 45
    EXPECT_NEAR(naiveSpan.right, -448.7F, 1e-2F); // moved left by 45
    const float center = 0.5F * (naiveSpan.left + naiveSpan.right);
    EXPECT_NEAR(center, full.translation.x, 1e-2F); // shrinks about center
    EXPECT_GT(naiveSpan.left, fullSpan.left);       // left edge NOT fixed -> crack
}

TEST(HudBarFill, LeftAnchorCompensationKeepsLeftEdgeFixed) {
    const Util::Transform full = HpFullTransform();
    const float fullLeft = RenderedSpanX(full, kTexW).left; // -583.7
    const float fullWidth = 180.0F;

    for (float f : {1.0F, 0.75F, 0.5F, 0.25F, 0.0F}) {
        const Util::Transform fill = FractionFillLeftAnchor(full, f, kTexW);
        const SpanX s = RenderedSpanX(fill, kTexW);
        // Left edge invariant for EVERY fraction (LtR fill, pivot [0,0.5]).
        EXPECT_NEAR(s.left, fullLeft, 1e-2F) << "f=" << f;
        // Width scales exactly with the fraction.
        EXPECT_NEAR(s.right - s.left, f * fullWidth, 1e-2F) << "f=" << f;
        // Right edge grows rightward from the fixed left edge.
        EXPECT_NEAR(s.right, fullLeft + f * fullWidth, 1e-2F) << "f=" << f;
        // translation.y / scale.y untouched by the horizontal fill.
        EXPECT_FLOAT_EQ(fill.translation.y, full.translation.y) << "f=" << f;
        EXPECT_FLOAT_EQ(fill.scale.y, full.scale.y) << "f=" << f;
    }
}

TEST(HudBarFill, FractionClampedAndHalfMatchesHandCalc) {
    const Util::Transform full = HpFullTransform();
    // f=0.5: scale.x halves, translation.x shifts left by (1-0.5)*180/2 = 45.
    const Util::Transform half = FractionFillLeftAnchor(full, 0.5F, kTexW);
    EXPECT_NEAR(half.scale.x, full.scale.x * 0.5F, 1e-5F);
    EXPECT_NEAR(half.translation.x, full.translation.x - 45.0F, 1e-2F); // -538.7
    EXPECT_NEAR(half.translation.x, -538.7F, 1e-2F);
    // Out-of-range fractions clamp to [0,1].
    EXPECT_FLOAT_EQ(FractionFillLeftAnchor(full, 1.5F, kTexW).scale.x, full.scale.x);
    EXPECT_FLOAT_EQ(FractionFillLeftAnchor(full, -0.3F, kTexW).scale.x, 0.0F);
}

// =====================================================================
// 3c. Report -- print the measured PTSD coordinates next to the design
//     hand-calc values, so the acceptance comparison is visible in the log.
// =====================================================================

TEST(HudLayoutParked, ReportMeasuredCoords) {
    const LayoutDoc doc = LayoutDoc::ParseString(kFixture);
    ASSERT_TRUE(doc.ok());
    const LayoutNode *bg = doc.Find("state_bar/bg");
    ASSERT_NE(bg, nullptr);

    const auto panel = ScreenRectToPtsd(ScreenRect{6.0F, 6.0F, 237.0F, 117.0F}, 79.0F, 39.0F);
    std::printf("\n--- measured PTSD coords (sprite centers) vs design hand-calc ---\n");
    std::printf("  bg panel (6,6,237,117)        -> (%8.3f, %8.3f)   design (-515.500, 295.500)\n",
                panel.translation.x, panel.translation.y);
    EXPECT_NEAR(panel.translation.x, -515.5F, 1e-2F);
    EXPECT_NEAR(panel.translation.y, 295.5F, 1e-2F);

    const Vec2 delta = ParkedDelta(bg->rect, Vec2{6.0F, 6.0F});
    std::printf("  parked delta (anchor bg->(6,6)) = (%.3f, %.3f)   design (-24.000, 148.940)\n",
                delta.x, delta.y);
    EXPECT_NEAR(delta.x, -24.0F, 1e-2F);
    EXPECT_NEAR(delta.y, 148.94F, 1e-2F);
    struct Row {
        const char *path;
        float dx, dy;
    };
    const Row rows[] = {
        {"state_bar/hp_bar/img", -493.7F, 327.46F},
        {"state_bar/armor_bar/img", -493.7F, 296.06F},
        {"state_bar/energy_bar/img", -493.7F, 264.66F},
    };
    for (const auto &r : rows) {
        const LayoutNode *n = doc.Find(r.path);
        ASSERT_NE(n, nullptr) << r.path;
        ASSERT_EQ(n->visual.size(), 1U) << r.path;
        const ScreenRect s = ShiftRect(n->rect, delta);
        const auto t = ScreenRectToPtsd(s, n->visual[0].texW, n->visual[0].texH);
        std::printf("  %-26s -> (%8.3f, %8.3f)   design (%8.3f, %8.3f)\n", r.path,
                    t.translation.x, t.translation.y, r.dx, r.dy);
        EXPECT_NEAR(t.translation.x, r.dx, 1e-2F) << r.path;
        EXPECT_NEAR(t.translation.y, r.dy, 1e-2F) << r.path;
    }
    std::printf("----------------------------------------------------------------\n");
}

// =====================================================================
// 4. Real-file smoke -- the loader reads the actual Canvas.layout.json
//    (best-effort: skipped if the repo file is not reachable from the test).
// =====================================================================

TEST(HudLayoutLoader, ReadsRealCanvasFile) {
    const std::string path =
        std::string(RESOURCE_DIR) + "/../docs/layout/Canvas.layout.json";
    const LayoutDoc doc = LayoutDoc::Load(path);
    if (!doc.ok()) {
        GTEST_SKIP() << "Canvas.layout.json not reachable at " << path;
    }
    const LayoutNode *hp = doc.Find("state_bar/hp_bar/img");
    ASSERT_NE(hp, nullptr);
    ASSERT_EQ(hp->visual.size(), 1U);
    EXPECT_EQ(hp->visual[0].spriteName, "ui_12");
    EXPECT_NEAR(hp->rect.left, 80.3F, 1e-2F);
    EXPECT_NEAR(hp->rect.top, -127.4F, 1e-2F);

    const LayoutNode *stateBar = doc.Find("state_bar");
    ASSERT_NE(stateBar, nullptr);
    EXPECT_EQ(ClassifyNode(*stateBar), Parked::Parked);

    const LayoutNode *control = doc.Find("control");
    ASSERT_NE(control, nullptr);
    EXPECT_EQ(ClassifyNode(*control), Parked::Ambiguous);
}

// NOLINTEND(readability-magic-numbers)
