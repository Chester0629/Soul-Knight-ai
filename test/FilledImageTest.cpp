#include <gtest/gtest.h>

#include "Util/FilledImage.hpp"

using Util::FilledQuad;
using Util::VerticalBottomFilledQuad;

// The UV-crop geometry is pure (no GL) -> unit-testable + the ultracode anchor.
// verts/uvs layout (TL,BL,BR,TR; (x,y)/(u,v) interleaved):
//   [0]TL.x [1]TL.y [2]BL.x [3]BL.y [4]BR.x [5]BR.y [6]TR.x [7]TR.y
// Bottom edge = BL.y(verts[3]) & BR.y(verts[5]) -- FIXED at -0.5 (slot bottom anchor).
// Top edge    = TL.y(verts[1]) & TR.y(verts[7]) = -0.5 + fraction.
// UV.v top    = TL.v(uvs[1]) & TR.v(uvs[7]) = 1 - fraction; bottom v = 1 (image bottom).

// NOLINTBEGIN(readability-magic-numbers)

TEST(FilledImageGeom, FullFractionIsTheWholeQuad) {
    const FilledQuad q = VerticalBottomFilledQuad(1.0F);
    EXPECT_FLOAT_EQ(q.verts[1], 0.5F);  // top edge at the very top
    EXPECT_FLOAT_EQ(q.verts[7], 0.5F);
    EXPECT_FLOAT_EQ(q.verts[3], -0.5F); // bottom edge
    EXPECT_FLOAT_EQ(q.verts[5], -0.5F);
    EXPECT_FLOAT_EQ(q.uvs[1], 0.0F);    // UV.v top = 0
    EXPECT_FLOAT_EQ(q.uvs[7], 0.0F);
    EXPECT_FLOAT_EQ(q.uvs[3], 1.0F);    // UV.v bottom = 1
    EXPECT_FLOAT_EQ(q.uvs[5], 1.0F);
}

TEST(FilledImageGeom, HalfFractionCropsBottomHalfBottomAnchored) {
    const FilledQuad q = VerticalBottomFilledQuad(0.5F);
    EXPECT_FLOAT_EQ(q.verts[1], 0.0F);  // top edge = -0.5 + 0.5 = 0
    EXPECT_FLOAT_EQ(q.verts[7], 0.0F);
    EXPECT_FLOAT_EQ(q.verts[3], -0.5F); // bottom FIXED
    EXPECT_FLOAT_EQ(q.verts[5], -0.5F);
    EXPECT_FLOAT_EQ(q.uvs[1], 0.5F);    // UV.v top = 1 - 0.5 = 0.5
    EXPECT_FLOAT_EQ(q.uvs[7], 0.5F);
    EXPECT_FLOAT_EQ(q.uvs[3], 1.0F);    // image-bottom anchored (the tapered point)
    // -> visible UV.v in [0.5, 1] = the BOTTOM half of the image.
}

TEST(FilledImageGeom, ZeroFractionIsDegenerateZeroHeight) {
    const FilledQuad q = VerticalBottomFilledQuad(0.0F);
    EXPECT_FLOAT_EQ(q.verts[1], -0.5F); // top == bottom -> 0 height -> invisible
    EXPECT_FLOAT_EQ(q.verts[3], -0.5F);
    EXPECT_FLOAT_EQ(q.uvs[1], 1.0F);    // UV.v top = 1
}

TEST(FilledImageGeom, BottomEdgeFixedAndTopTracksFraction) {
    for (float f : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
        const FilledQuad q = VerticalBottomFilledQuad(f);
        EXPECT_FLOAT_EQ(q.verts[3], -0.5F) << "BL.y must stay -0.5 at f=" << f;
        EXPECT_FLOAT_EQ(q.verts[5], -0.5F) << "BR.y must stay -0.5 at f=" << f;
        EXPECT_FLOAT_EQ(q.uvs[3], 1.0F) << "BL.v image-bottom anchored at f=" << f;
        EXPECT_FLOAT_EQ(q.verts[1], -0.5F + f) << "top edge tracks f=" << f;
        EXPECT_FLOAT_EQ(q.uvs[1], 1.0F - f) << "UV.v top tracks f=" << f;
    }
}

TEST(FilledImageGeom, FractionClampedTo01) {
    EXPECT_FLOAT_EQ(VerticalBottomFilledQuad(1.5F).verts[1], 0.5F);   // clamp high -> f=1
    EXPECT_FLOAT_EQ(VerticalBottomFilledQuad(-0.3F).verts[1], -0.5F); // clamp low -> f=0
}

// NOLINTEND(readability-magic-numbers)
