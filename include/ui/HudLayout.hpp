#ifndef GAME_UI_HUD_LAYOUT_HPP
#define GAME_UI_HUD_LAYOUT_HPP

#include <string>
#include <vector>

#include "Util/Transform.hpp"

// HUD coordinate-logic foundation (HUD landing Phase 1).
//
// Pure, render-free, asset-free module that turns the layout-extractor's baked
// `screen_rect` data into PTSD sprite transforms. It does three things and only
// these three:
//   1. Converter   -- screen_rect (1280x720, top-left, Y-down) -> Util::Transform
//                     (PTSD: center origin, Y-up, pixels; sprite drawn CENTERED
//                     at translation).
//   2. Loader      -- read-only nlohmann_json reader over docs/layout/**.layout.json.
//                     Reads ONLY the fields extract_layout.py already baked; never
//                     re-derives Unity RectTransform math in C++.
//   3. Parked      -- geometry-only "is this group's static absolute position
//                     trustworthy?" classifier + a per-element anchor override.
//
// CRITICAL design fact (verified against the engine): PTSD does NOT compose
// parent->child transforms (Renderer::Update collects m_ParentTransform but never
// applies it; GameObject::Draw uses only its own m_Transform). Therefore this
// module outputs an ABSOLUTE PTSD transform per element. There is no group /
// container / parent-offset abstraction: the parked shift is folded into each
// element's own absolute coordinates.

namespace Game::UI {

// ---------------------------------------------------------------------------
// Data carried straight out of the layout JSON (no recomputation).
// ---------------------------------------------------------------------------

/// A node rect in layout/screen space: top-left origin, Y-down, 1280x720.
struct ScreenRect {
    float left = 0.0F;
    float top = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
};

/// Rendered extent in screen space (Left, Top, Right, Bottom), Y-down.
struct Extent {
    float l = 0.0F;
    float t = 0.0F;
    float r = 0.0F;
    float b = 0.0F;
};

/// A 2D screen-space offset (top-left origin, Y-down) used by the parked shift.
struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Rgba {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

/// Sprite 9-slice border (sprite px). Absent in the JSON => all zero.
struct Border {
    float left = 0.0F;
    float bottom = 0.0F;
    float right = 0.0F;
    float top = 0.0F;
};

/// One visual attached to a node (an Image fill/panel or a Text). All fields are
/// read verbatim from the baked JSON; the asset stage consumes them later.
struct Visual {
    std::string kind;       ///< "Image" or "Text".
    std::string spritePng;  ///< png path as baked (e.g. "docs\\layout\\crops\\ui_12.png").
    std::string spriteName; ///< e.g. "ui_12" / "ui_15"; empty for Text.
    float texW = 0.0F;      ///< native sprite px (sprite_rect.w) -> converter scale.
    float texH = 0.0F;      ///< native sprite px (sprite_rect.h).
    Rgba tint;              ///< Image/Text color (tint for pre-baked PNG).
    int imageType = 0;      ///< Unity Image.type (0 simple, 1 sliced, ...).
    Border border;          ///< 9-slice border; hasBorder false => zeros.
    bool hasBorder = false;
    std::string text;       ///< Text content; empty for Image.
};

/// A layout node. `visual` and `children` may both be present or empty.
struct LayoutNode {
    std::string name;
    bool active = true;
    ScreenRect rect;
    std::vector<Visual> visual;
    std::vector<LayoutNode> children;
};

/// Trustworthiness of a group's STATIC absolute position (geometry only).
enum class Parked {
    Normal,    ///< static screen_rect IS the final on-screen position.
    Parked,    ///< rendered fully off-canvas -> static position NOT trustworthy.
    Ambiguous, ///< straddles a canvas edge -> flagged, never force-binary.
};

// ---------------------------------------------------------------------------
// 1. Converter (pure function).
// ---------------------------------------------------------------------------

/// screen_rect -> PTSD transform. translation is the sprite CENTER in PTSD pixels
/// (center origin, Y-up); scale maps native sprite px to the slot's px size.
///   translation.x = left + w/2 - canvasW/2
///   translation.y = canvasH/2 - (top + h/2)        // single Y-flip about center
///   scale         = (w/texW, h/texH)               // 1 if texW/texH is 0
Util::Transform ScreenRectToPtsd(const ScreenRect &rect, float texW, float texH,
                                 float canvasW = 1280.0F, float canvasH = 720.0F);

// ---------------------------------------------------------------------------
// 3a. Parked / slide-in detection (pure JSON geometry, zero reverse).
// ---------------------------------------------------------------------------

/// Rendered extent of a group: its own rect if non-degenerate, else the union
/// bbox of its non-degenerate descendants (container nodes are 0x0 in Unity).
Extent RenderedExtent(const LayoutNode &group);

/// Classify a rendered extent against the canvas.
Parked Classify(const Extent &e, float canvasW = 1280.0F, float canvasH = 720.0F);

/// Convenience: RenderedExtent + Classify.
Parked ClassifyNode(const LayoutNode &group, float canvasW = 1280.0F,
                    float canvasH = 720.0F);

// ---------------------------------------------------------------------------
// 3b. External-anchor override (per-element absolute coords; NO container move).
// ---------------------------------------------------------------------------

/// delta = targetTopLeft - refChildStatic.topLeft  (screen space, Y-down).
/// The client supplies targetTopLeft (a known on-screen anchor); this module
/// never derives where code repositions a parked group at runtime.
Vec2 ParkedDelta(const ScreenRect &refChildStatic, Vec2 targetTopLeft);

/// Rigidly shift a rect by `delta` (sizes preserved). Apply to EACH element of a
/// parked group, then run ScreenRectToPtsd -> that element's absolute PTSD coords.
ScreenRect ShiftRect(const ScreenRect &rect, Vec2 delta);

// ---------------------------------------------------------------------------
// 3c. Fractional fill (left-anchored / LtR) -- the Path-A render seam.
// ---------------------------------------------------------------------------
//
// PTSD's Image quad is centered (-0.5..0.5) and ConvertToUniformBufferData scales
// it ABOUT THE CENTER (model = translate(translation)*scale(transform.scale*size),
// TransformUtils.cpp:33-35). So multiplying transform.scale.x by a fraction f
// shrinks BOTH edges toward translation.x -- it is NOT the left-anchored fill that
// Unity's vitals bars use (pivot/anchor [0,0.5], sizeDelta.x shrinks rightward off
// a fixed left edge). FractionFillLeftAnchor compensates: it scales AND translates
// so the bar's LEFT edge stays put while it shrinks to the right.

/// The on-screen horizontal span [left, right] a transform's centered quad covers,
/// in PTSD center-origin px. left/right = translation.x -/+ 0.5*scale.x*texW.
/// Mirrors the engine quad math, so it predicts the rendered edges exactly WHEN
/// rotation == 0 (the case for the axis-aligned HUD bars). For a rotated quad the
/// axis-aligned extent differs and this is only the unrotated half-width.
struct SpanX {
    float left = 0.0F;
    float right = 0.0F;
};
SpanX RenderedSpanX(const Util::Transform &t, float texW);

/// Left-anchored (LtR) fractional fill. Given the FULL-bar transform (fraction 1)
/// and the native sprite width texW, return a transform whose rendered width is
/// fraction*fullWidth with the LEFT edge unchanged:
///   w                 = full.scale.x * texW              // full on-screen width px
///   out.scale.x       = full.scale.x * fraction
///   out.translation.x = full.translation.x - 0.5*(1-fraction)*w
/// fraction is clamped to [0,1]; scale.y / translation.y / rotation are preserved.
Util::Transform FractionFillLeftAnchor(const Util::Transform &full, float fraction,
                                       float texW);

// ---------------------------------------------------------------------------
// 2. Layout-JSON loader (read-only, nlohmann_json under the hood).
// ---------------------------------------------------------------------------

class LayoutDoc {
public:
    /// Parse from a file path. ok() is false (and Root() empty) on read/parse failure.
    static LayoutDoc Load(const std::string &path);
    /// Parse from an in-memory JSON string (used by tests / embedded fixtures).
    static LayoutDoc ParseString(const std::string &json);

    bool ok() const { return m_Ok; }
    float canvasW() const { return m_CanvasW; }
    float canvasH() const { return m_CanvasH; }

    /// The synthetic top of the tree (the JSON's first root, e.g. "Canvas").
    const LayoutNode &Root() const { return m_Root; }

    /// Resolve a node by name-path under Root(), e.g. "state_bar/hp_bar/img".
    /// Returns nullptr if any segment is missing.
    ///
    /// CONTRACT / KNOWN LIMITATION: at each level, Find takes the FIRST child
    /// whose name matches; it assumes the name-path is unique. This is not always
    /// true in Canvas.layout.json -- e.g. `btn_home` exists twice under different
    /// parents (a parked pause-menu instance at (380,-320) and the on-screen
    /// bottom-left instance at (60,570)), so a bare-name lookup would silently take
    /// the first. The `state_bar` subtree this HUD consumes IS verified unique
    /// (the on-disk test ReadsRealCanvasFile resolves state_bar/hp_bar/img to the
    /// correct node), so the HUD is safe today. Future screens with repeated
    /// structure (notably co-op / multi-player UIs) MUST confirm path uniqueness
    /// before relying on Find, or add disambiguation (by index / rect / predicate).
    /// This is a documented limitation, not a bug to fix now.
    const LayoutNode *Find(const std::string &namePath) const;

private:
    LayoutNode m_Root;
    bool m_Ok = false;
    float m_CanvasW = 1280.0F;
    float m_CanvasH = 720.0F;
};

} // namespace Game::UI

#endif /* GAME_UI_HUD_LAYOUT_HPP */
