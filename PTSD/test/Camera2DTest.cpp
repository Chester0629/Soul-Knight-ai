#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

#include "Core/Camera2D.hpp"

using Core::Camera2D;

namespace {

// Transform a world point through the camera's view matrix.
glm::vec2 ApplyView(const Camera2D &cam, glm::vec2 world) {
    const glm::vec4 v = cam.GetViewMatrix() * glm::vec4(world, 0.0F, 1.0F);
    return glm::vec2(v.x, v.y);
}

} // namespace

// NOLINTBEGIN(readability-magic-numbers)

TEST(Camera2DTest, DefaultViewIsIdentity) {
    Camera2D cam;
    const glm::vec2 r = ApplyView(cam, glm::vec2(7.0F, -3.0F));
    EXPECT_NEAR(r.x, 7.0F, 1e-4F);
    EXPECT_NEAR(r.y, -3.0F, 1e-4F);
}

TEST(Camera2DTest, CenterMapsToViewOrigin) {
    Camera2D cam;
    cam.SetPosition(glm::vec2(10.0F, 20.0F));
    const glm::vec2 r = ApplyView(cam, glm::vec2(10.0F, 20.0F));
    EXPECT_NEAR(r.x, 0.0F, 1e-4F);
    EXPECT_NEAR(r.y, 0.0F, 1e-4F);
}

TEST(Camera2DTest, ZoomScalesAroundCenter) {
    Camera2D cam;
    cam.SetPosition(glm::vec2(0.0F, 0.0F));
    cam.SetZoom(2.0F);
    const glm::vec2 r = ApplyView(cam, glm::vec2(10.0F, 0.0F));
    EXPECT_NEAR(r.x, 20.0F, 1e-4F);
    EXPECT_NEAR(r.y, 0.0F, 1e-4F);
}

TEST(Camera2DTest, NonPositiveZoomIgnored) {
    Camera2D cam;
    cam.SetZoom(3.0F);
    cam.SetZoom(-1.0F); // ignored
    cam.SetZoom(0.0F);  // ignored
    EXPECT_FLOAT_EQ(cam.GetZoom(), 3.0F);
}

TEST(Camera2DTest, FollowLerpsTowardTarget) {
    Camera2D cam;
    cam.SetPosition(glm::vec2(0.0F, 0.0F));
    cam.Follow(glm::vec2(10.0F, 0.0F), 0.5F);
    EXPECT_NEAR(cam.GetPosition().x, 5.0F, 1e-4F);
    cam.Follow(glm::vec2(10.0F, 0.0F), 1.0F);
    EXPECT_NEAR(cam.GetPosition().x, 10.0F, 1e-4F);
}

TEST(Camera2DTest, TraumaClampedToOne) {
    Camera2D cam;
    cam.AddTrauma(5.0F);
    EXPECT_FLOAT_EQ(cam.GetTrauma(), 1.0F);
}

TEST(Camera2DTest, TraumaDecaysToZero) {
    Camera2D cam;
    cam.AddTrauma(1.0F);
    EXPECT_GT(cam.GetTrauma(), 0.0F);
    cam.Update(2000.0F); // default decay reaches 0 within ~1000ms
    EXPECT_FLOAT_EQ(cam.GetTrauma(), 0.0F);
}

TEST(Camera2DTest, NoShakeOffsetWithoutTrauma) {
    Camera2D cam;
    cam.Update(16.0F);
    EXPECT_FLOAT_EQ(cam.GetShakeOffset().x, 0.0F);
    EXPECT_FLOAT_EQ(cam.GetShakeOffset().y, 0.0F);
}

TEST(Camera2DTest, ShakeOffsetBoundedByMaxShake) {
    Camera2D cam;
    cam.SetMaxShake(16.0F);
    cam.AddTrauma(1.0F);
    cam.Update(16.0F);
    EXPECT_LE(std::abs(cam.GetShakeOffset().x), 16.0F + 1e-3F);
    EXPECT_LE(std::abs(cam.GetShakeOffset().y), 16.0F + 1e-3F);
}

TEST(Camera2DTest, ShakeIsDeterministic) {
    Camera2D a;
    Camera2D b;
    a.AddTrauma(1.0F);
    b.AddTrauma(1.0F);
    a.Update(16.0F);
    b.Update(16.0F);
    EXPECT_FLOAT_EQ(a.GetShakeOffset().x, b.GetShakeOffset().x);
    EXPECT_FLOAT_EQ(a.GetShakeOffset().y, b.GetShakeOffset().y);
}

// NOLINTEND(readability-magic-numbers)
