#include "ui/HudLayout.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace Game::UI {

namespace {

using nlohmann::json;

// Read a JSON number as float with a default; tolerant of missing/non-number.
float Getf(const json &j, const char *key, float def) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_number()) {
        return def;
    }
    return it->get<float>();
}

ScreenRect ParseRect(const json &j) {
    ScreenRect r;
    auto it = j.find("screen_rect");
    if (it == j.end() || !it->is_object()) {
        return r;
    }
    const json &sr = *it;
    r.left = Getf(sr, "left", 0.0F);
    r.top = Getf(sr, "top", 0.0F);
    r.w = Getf(sr, "w", 0.0F);
    r.h = Getf(sr, "h", 0.0F);
    return r;
}

Rgba ParseColor(const json &j) {
    Rgba c;
    auto it = j.find("color");
    if (it == j.end() || !it->is_object()) {
        return c;
    }
    const json &col = *it;
    c.r = Getf(col, "r", 1.0F);
    c.g = Getf(col, "g", 1.0F);
    c.b = Getf(col, "b", 1.0F);
    c.a = Getf(col, "a", 1.0F);
    return c;
}

Visual ParseVisual(const json &j) {
    Visual v;
    v.kind = j.value("kind", std::string{});
    v.imageType = j.value("image_type", 0);
    v.tint = ParseColor(j);
    v.text = j.value("text", std::string{});

    auto sp = j.find("sprite");
    if (sp != j.end() && sp->is_object()) {
        v.spritePng = sp->value("png", std::string{});
        v.spriteName = sp->value("name", std::string{});
        auto rect = sp->find("sprite_rect");
        if (rect != sp->end() && rect->is_object()) {
            v.texW = Getf(*rect, "w", 0.0F);
            v.texH = Getf(*rect, "h", 0.0F);
        }
        auto bd = sp->find("border");
        if (bd != sp->end() && bd->is_object()) {
            v.hasBorder = true;
            v.border.left = Getf(*bd, "left", 0.0F);
            v.border.bottom = Getf(*bd, "bottom", 0.0F);
            v.border.right = Getf(*bd, "right", 0.0F);
            v.border.top = Getf(*bd, "top", 0.0F);
        }
    }
    return v;
}

LayoutNode ParseNode(const json &j) {
    LayoutNode n;
    n.name = j.value("name", std::string{});
    n.active = j.value("active", true);
    n.rect = ParseRect(j);

    auto vis = j.find("visual");
    if (vis != j.end() && vis->is_array()) {
        for (const auto &v : *vis) {
            if (v.is_object()) {
                n.visual.push_back(ParseVisual(v));
            }
        }
    }
    auto ch = j.find("children");
    if (ch != j.end() && ch->is_array()) {
        for (const auto &c : *ch) {
            if (c.is_object()) {
                n.children.push_back(ParseNode(c));
            }
        }
    }
    return n;
}

bool Degenerate(const ScreenRect &r) { return r.w <= 0.0F || r.h <= 0.0F; }

// Accumulate the union bbox of all non-degenerate rects in the subtree.
void UnionInto(const LayoutNode &node, bool includeSelf, Extent &acc, bool &any) {
    if (includeSelf && !Degenerate(node.rect)) {
        const float l = node.rect.left;
        const float t = node.rect.top;
        const float r = node.rect.left + node.rect.w;
        const float b = node.rect.top + node.rect.h;
        if (!any) {
            acc = {l, t, r, b};
            any = true;
        } else {
            acc.l = l < acc.l ? l : acc.l;
            acc.t = t < acc.t ? t : acc.t;
            acc.r = r > acc.r ? r : acc.r;
            acc.b = b > acc.b ? b : acc.b;
        }
    }
    for (const auto &c : node.children) {
        UnionInto(c, true, acc, any);
    }
}

LayoutNode ParseFirstRoot(const json &doc, float &cw, float &ch, bool &ok) {
    LayoutNode root;
    auto canvas = doc.find("canvas");
    if (canvas != doc.end() && canvas->is_object()) {
        cw = Getf(*canvas, "w", cw);
        ch = Getf(*canvas, "h", ch);
    }
    auto roots = doc.find("roots");
    if (roots != doc.end() && roots->is_array() && !roots->empty() &&
        roots->front().is_object()) {
        root = ParseNode(roots->front());
        ok = true;
    }
    return root;
}

} // namespace

// ---------------------------------------------------------------------------
// Converter.
// ---------------------------------------------------------------------------

Util::Transform ScreenRectToPtsd(const ScreenRect &rect, float texW, float texH,
                                 float canvasW, float canvasH) {
    Util::Transform out;
    out.translation.x = rect.left + (rect.w * 0.5F) - (canvasW * 0.5F);
    out.translation.y = (canvasH * 0.5F) - (rect.top + (rect.h * 0.5F));
    out.scale.x = (texW != 0.0F) ? (rect.w / texW) : 1.0F;
    out.scale.y = (texH != 0.0F) ? (rect.h / texH) : 1.0F;
    return out;
}

// ---------------------------------------------------------------------------
// Parked detection.
// ---------------------------------------------------------------------------

Extent RenderedExtent(const LayoutNode &group) {
    if (!Degenerate(group.rect)) {
        return {group.rect.left, group.rect.top, group.rect.left + group.rect.w,
                group.rect.top + group.rect.h};
    }
    // 0x0 container: union of descendants' non-degenerate rects.
    Extent acc;
    bool any = false;
    UnionInto(group, /*includeSelf=*/false, acc, any);
    if (!any) {
        // No drawable descendant: fall back to the degenerate point.
        return {group.rect.left, group.rect.top, group.rect.left, group.rect.top};
    }
    return acc;
}

Parked Classify(const Extent &e, float canvasW, float canvasH) {
    const bool fullyOutside =
        e.b <= 0.0F || e.t >= canvasH || e.r <= 0.0F || e.l >= canvasW;
    if (fullyOutside) {
        return Parked::Parked;
    }
    const bool straddles =
        e.t < 0.0F || e.b > canvasH || e.l < 0.0F || e.r > canvasW;
    if (straddles) {
        return Parked::Ambiguous;
    }
    return Parked::Normal;
}

Parked ClassifyNode(const LayoutNode &group, float canvasW, float canvasH) {
    return Classify(RenderedExtent(group), canvasW, canvasH);
}

// ---------------------------------------------------------------------------
// Anchor override.
// ---------------------------------------------------------------------------

Vec2 ParkedDelta(const ScreenRect &refChildStatic, Vec2 targetTopLeft) {
    return {targetTopLeft.x - refChildStatic.left,
            targetTopLeft.y - refChildStatic.top};
}

ScreenRect ShiftRect(const ScreenRect &rect, Vec2 delta) {
    return {rect.left + delta.x, rect.top + delta.y, rect.w, rect.h};
}

// ---------------------------------------------------------------------------
// Fractional fill (left-anchored / LtR).
// ---------------------------------------------------------------------------

SpanX RenderedSpanX(const Util::Transform &t, float texW) {
    const float half = 0.5F * t.scale.x * texW;
    return {t.translation.x - half, t.translation.x + half};
}

Util::Transform FractionFillLeftAnchor(const Util::Transform &full, float fraction,
                                       float texW) {
    const float f = fraction < 0.0F ? 0.0F : (fraction > 1.0F ? 1.0F : fraction);
    const float w = full.scale.x * texW; // full on-screen bar width (px)
    Util::Transform out = full;
    out.scale.x = full.scale.x * f;
    out.translation.x = full.translation.x - (0.5F * (1.0F - f) * w);
    return out;
}

// ---------------------------------------------------------------------------
// Loader.
// ---------------------------------------------------------------------------

LayoutDoc LayoutDoc::ParseString(const std::string &jsonText) {
    LayoutDoc doc;
    json parsed = json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return doc;
    }
    // This module is the SCREEN-SPACE coordinate layer: it consumes the baked
    // absolute `screen_rect` fields. Prefab-local layout files bake `local_rect`
    // instead (coordinate_space == "prefab_local"); reading one here would yield
    // silent 0x0 rects, so reject anything that is not screen-space rather than
    // return geometry that looks valid but is all zero. (Field absent => assume
    // screen-space, for forward tolerance.)
    if (parsed.value("coordinate_space", std::string{"screen"}) != "screen") {
        return doc; // ok() stays false
    }
    doc.m_Root = ParseFirstRoot(parsed, doc.m_CanvasW, doc.m_CanvasH, doc.m_Ok);
    return doc;
}

LayoutDoc LayoutDoc::Load(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return LayoutDoc{};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ParseString(ss.str());
}

const LayoutNode *LayoutDoc::Find(const std::string &namePath) const {
    const LayoutNode *cur = &m_Root;
    std::size_t pos = 0;
    while (pos <= namePath.size()) {
        const std::size_t slash = namePath.find('/', pos);
        const std::string seg = namePath.substr(
            pos, slash == std::string::npos ? std::string::npos : slash - pos);
        if (!seg.empty()) {
            const LayoutNode *next = nullptr;
            for (const auto &c : cur->children) {
                if (c.name == seg) {
                    next = &c;
                    break;
                }
            }
            if (next == nullptr) {
                return nullptr;
            }
            cur = next;
        }
        if (slash == std::string::npos) {
            break;
        }
        pos = slash + 1;
    }
    return cur == &m_Root ? nullptr : cur;
}

} // namespace Game::UI
