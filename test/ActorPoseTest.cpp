#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include "entities/ActorVisual.hpp"
#include "entities/SpriteAnchor.hpp"
#include "game/ActorPose.hpp"
#include "game/WorldScale.hpp"

// Slice 4 (characters). Pure maths of the pose: facing, Animator clip choice, weapon pose / muzzle, lock
// target. No GL. FAITHFUL sources are quoted in include/game/ActorPose.hpp.
namespace {

namespace AP = Game::ActorPose;
constexpr float kPi = 3.14159265358979323846F;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

// ---- facing (RGController.set_facing: the img subtree turns to RotLeft / RotRight) -----------------

TEST(ActorPoseTest, FacingFollowsTheSideOfTheAim) {
    EXPECT_EQ(AP::ResolveFacing(1, -1.0F), -1);
    EXPECT_EQ(AP::ResolveFacing(-1, 1.0F), 1);
    EXPECT_EQ(AP::ResolveFacing(1, 0.5F), 1);
    EXPECT_EQ(AP::ResolveFacing(-1, -0.5F), -1);
}

TEST(ActorPoseTest, VerticalAimKeepsTheLastSide) {
    // Ground truth: aiming straight down (crops 2088 / 2196) keeps the side the hero already faced.
    EXPECT_EQ(AP::ResolveFacing(1, 0.0F), 1);
    EXPECT_EQ(AP::ResolveFacing(-1, 0.0F), -1);
    EXPECT_EQ(AP::ResolveFacing(-1, AP::kFacingDeadZone), -1);        // boundary: inside the dead zone
    EXPECT_EQ(AP::ResolveFacing(-1, -AP::kFacingDeadZone), -1);
    EXPECT_EQ(AP::ResolveFacing(-1, AP::kFacingDeadZone * 1.01F), 1);  // just outside flips
    EXPECT_EQ(AP::ResolveFacing(1, -AP::kFacingDeadZone * 1.01F), -1);
}

TEST(ActorPoseTest, FacingIgnoresNonFiniteInputAndNormalisesPrevious) {
    EXPECT_EQ(AP::ResolveFacing(-1, kNaN), -1);
    EXPECT_EQ(AP::ResolveFacing(1, kNaN), 1);
    EXPECT_EQ(AP::ResolveFacing(-1, kInf), -1); // non-finite is not a side
    EXPECT_EQ(AP::ResolveFacing(1, -kInf), 1);
    EXPECT_EQ(AP::ResolveFacing(0, 0.0F), 1);   // 0 / garbage previous counts as +1
    EXPECT_EQ(AP::ResolveFacing(7, 0.0F), 1);
    EXPECT_EQ(AP::ResolveFacing(-7, 0.0F), -1);
    EXPECT_EQ(AP::ResolveFacing(0, -1.0F), -1);
}

// ---- Animator clip choice (controller: ide <-> run on the bool, any -> dead) ----------------------

TEST(ActorPoseTest, ClipStateMachine) {
    using C = AP::ActorClip;
    EXPECT_EQ(AP::SelectClip(false, false), C::Idle);
    EXPECT_EQ(AP::SelectClip(false, true), C::Run);
    EXPECT_EQ(AP::SelectClip(true, false), C::Dead);
    EXPECT_EQ(AP::SelectClip(true, true), C::Dead); // dead beats run
}

TEST(ActorPoseTest, MovingMeansFasterThanTheEpsilon) {
    // 20 ms frame: 1 px/s epsilon == 0.02 px of displacement.
    EXPECT_TRUE(AP::IsMoving({1.0F, 0.0F}, 20.0F));
    EXPECT_TRUE(AP::IsMoving({0.0F, -0.5F}, 20.0F));
    EXPECT_FALSE(AP::IsMoving({0.0F, 0.0F}, 20.0F));
    EXPECT_FALSE(AP::IsMoving({0.01F, 0.0F}, 20.0F)); // 0.5 px/s: depenetration jitter, not a run
    // Boundary: exactly kRunSpeedEpsPxPerSec is NOT moving, a hair above is.
    EXPECT_FALSE(AP::IsMoving({AP::kRunSpeedEpsPxPerSec * 0.02F, 0.0F}, 20.0F));
    EXPECT_TRUE(AP::IsMoving({AP::kRunSpeedEpsPxPerSec * 0.0201F, 0.0F}, 20.0F));
    // Direction does not matter, only speed.
    EXPECT_EQ(AP::IsMoving({3.0F, 4.0F}, 1000.0F), AP::IsMoving({-4.0F, -3.0F}, 1000.0F));
}

TEST(ActorPoseTest, MovingRejectsNonFiniteAndNonPositiveDt) {
    EXPECT_FALSE(AP::IsMoving({kNaN, 0.0F}, 20.0F));
    EXPECT_FALSE(AP::IsMoving({0.0F, kInf}, 20.0F));
    EXPECT_FALSE(AP::IsMoving({10.0F, 0.0F}, 0.0F));
    EXPECT_FALSE(AP::IsMoving({10.0F, 0.0F}, -5.0F));
    EXPECT_FALSE(AP::IsMoving({10.0F, 0.0F}, kNaN));
    EXPECT_FALSE(AP::IsMoving({1.0e30F, 1.0e30F}, 20.0F)); // |d|^2 overflows float -> a teleport, not a run
    EXPECT_FALSE(AP::IsMoving({1.0e20F, 0.0F}, 1.0e-30F)); // speed overflows -> not a run
}

// ---- enemy / boss facing source (PORT CHOICE T-69) ----------------------------------------------------

TEST(ActorPoseTest, EnemyFacesTheHeroWhileAwakeElseItsHeading) {
    // awake (hero in its room): the hero's side, whatever the heading says
    const glm::vec2 self{100.0F, 0.0F};
    EXPECT_EQ(AP::ResolveFacing(1, AP::EnemyFaceDx(true, self, {40.0F, 0.0F}, {1.0F, 0.0F})), -1); // hero to the left
    EXPECT_EQ(AP::ResolveFacing(-1, AP::EnemyFaceDx(true, self, {400.0F, 0.0F}, {-1.0F, 0.0F})), 1);
    // asleep / other room: its own heading
    EXPECT_EQ(AP::ResolveFacing(1, AP::EnemyFaceDx(false, self, {40.0F, 0.0F}, {-1.0F, 0.3F})), -1);
    EXPECT_EQ(AP::ResolveFacing(-1, AP::EnemyFaceDx(false, self, {40.0F, 0.0F}, {1.0F, -0.3F})), 1);
    // hero straight above / below (dx == 0) or a still enemy (heading.x == 0): the last side is kept
    EXPECT_EQ(AP::ResolveFacing(-1, AP::EnemyFaceDx(true, self, {100.0F, 50.0F}, {1.0F, 0.0F})), -1);
    EXPECT_EQ(AP::ResolveFacing(1, AP::EnemyFaceDx(false, self, {0.0F, 0.0F}, {0.0F, 1.0F})), 1);
    // a non-finite position never flips it
    EXPECT_EQ(AP::ResolveFacing(-1, AP::EnemyFaceDx(true, {kNaN, 0.0F}, {0.0F, 0.0F}, {1.0F, 0.0F})), -1);
    EXPECT_EQ(AP::ResolveFacing(1, AP::EnemyFaceDx(true, self, {-kInf, 0.0F}, {1.0F, 0.0F})), 1);
}

// ---- weapon pose / muzzle ---------------------------------------------------------------------

TEST(ActorPoseTest, WeaponLocalPointRotatesToTheAimAndFlipsWithTheFacing) {
    const glm::vec2 gp{19.2F, 4.8F}; // weapon_000 gun_point (0.6, 0.15) units * 32 px
    const auto near = [](glm::vec2 a, glm::vec2 b) {
        return std::fabs(a.x - b.x) < 1e-4F && std::fabs(a.y - b.y) < 1e-4F;
    };
    // facing +1: the plain rotation R(a) * (lx, ly).
    EXPECT_TRUE(near(AP::WeaponLocalToWorld(gp, {1.0F, 0.0F}, 1), {19.2F, 4.8F}));
    EXPECT_TRUE(near(AP::WeaponLocalToWorld(gp, {0.0F, 1.0F}, 1), {-4.8F, 19.2F}));
    EXPECT_TRUE(near(AP::WeaponLocalToWorld(gp, {-1.0F, 0.0F}, 1), {-19.2F, -4.8F}));
    EXPECT_TRUE(near(AP::WeaponLocalToWorld(gp, {0.0F, -1.0F}, 1), {4.8F, -19.2F}));
    // facing -1: the sprite is the x mirror, so ly flips: aiming left the muzzle is still on TOP of the barrel.
    EXPECT_TRUE(near(AP::WeaponLocalToWorld(gp, {-1.0F, 0.0F}, -1), {-19.2F, 4.8F}));
    EXPECT_TRUE(near(AP::WeaponLocalToWorld(gp, {0.0F, 1.0F}, -1), {4.8F, 19.2F}));
    EXPECT_TRUE(near(AP::WeaponLocalToWorld(gp, {0.0F, -1.0F}, -1), {-4.8F, -19.2F}));
    // The muzzle always sits |gp| from the hand whatever the aim / facing.
    for (const int f : {1, -1}) {
        for (float a = 0.0F; a < 2.0F * kPi; a += 0.3F) {
            const glm::vec2 m = AP::WeaponLocalToWorld(gp, {std::cos(a), std::sin(a)}, f);
            EXPECT_NEAR(glm::length(m), glm::length(gp), 1e-3F);
        }
    }
}

TEST(ActorPoseTest, NonFiniteOrZeroAimFallsBackToThePlusXAxis) {
    const glm::vec2 gp{19.2F, 4.8F};
    for (const glm::vec2 aim : {glm::vec2{0.0F, 0.0F}, glm::vec2{kNaN, 1.0F}, glm::vec2{1.0F, kInf}}) {
        const glm::vec2 m = AP::WeaponLocalToWorld(gp, aim, 1);
        EXPECT_NEAR(m.x, 19.2F, 1e-4F);
        EXPECT_NEAR(m.y, 4.8F, 1e-4F);
        EXPECT_TRUE(std::isfinite(m.x) && std::isfinite(m.y));
    }
    EXPECT_FLOAT_EQ(AP::AimAngleRad({0.0F, 0.0F}), 0.0F);
    EXPECT_FLOAT_EQ(AP::AimAngleRad({kNaN, 0.0F}), 0.0F);
}

TEST(ActorPoseTest, SpritePoseDrawsTheGunAlongTheAimWithTheSamePointAsTheMuzzle) {
    // Util::GameObject draws a point p (relative to the pivot) at T + R(rot) * (sx * px, sy * py) (about the
    // pivot, scale first, then rotation). The drawn position of a weapon-local point must equal the maths
    // used for the bullet spawn, for every aim and both facings.
    const glm::vec2 gp{19.2F, 4.8F};
    for (const int f : {1, -1}) {
        for (float a = -3.1F; a < 3.1F; a += 0.37F) {
            const glm::vec2 aim{std::cos(a), std::sin(a)};
            const AP::SpritePose pose = AP::WeaponSpritePose(aim, f);
            EXPECT_EQ(pose.scaleSignX, f < 0 ? -1.0F : 1.0F);
            EXPECT_GE(pose.rotationRad, -kPi - 1e-5F);
            EXPECT_LT(pose.rotationRad, kPi + 1e-5F);
            const glm::vec2 scaled{pose.scaleSignX * gp.x, gp.y};
            const float c = std::cos(pose.rotationRad);
            const float s = std::sin(pose.rotationRad);
            const glm::vec2 drawn{c * scaled.x - s * scaled.y, s * scaled.x + c * scaled.y};
            const glm::vec2 muzzle = AP::WeaponLocalToWorld(gp, aim, f);
            EXPECT_NEAR(drawn.x, muzzle.x, 2e-3F) << "facing " << f << " aim " << a;
            EXPECT_NEAR(drawn.y, muzzle.y, 2e-3F) << "facing " << f << " aim " << a;
            // and the sprite's +x axis (the barrel) points along the aim
            const glm::vec2 axis{c * pose.scaleSignX, s * pose.scaleSignX};
            EXPECT_NEAR(axis.x, aim.x, 1e-4F);
            EXPECT_NEAR(axis.y, aim.y, 1e-4F);
        }
    }
}

TEST(ActorPoseTest, HandMountMirrorsAboutTheRootAxis) {
    // c08 hand h1 = (0.4, 0.6) units = (12.8, 19.2) px; body centre 16 px above the feet.
    const glm::vec2 hand{12.8F, 19.2F};
    const glm::vec2 feet{0.0F, 16.0F};
    const glm::vec2 pos{100.0F, 200.0F};
    const glm::vec2 r = Game::ActorPose::HandWorld(pos, hand, feet, 1);
    const glm::vec2 l = Game::ActorPose::HandWorld(pos, hand, feet, -1);
    EXPECT_FLOAT_EQ(r.x, 112.8F);
    EXPECT_FLOAT_EQ(l.x, 87.2F);
    EXPECT_FLOAT_EQ(r.y, 203.2F);
    EXPECT_FLOAT_EQ(l.y, 203.2F);
    // c01: h1 (0, 0.5) == the body centre -> the hand IS Position() whatever the facing.
    const glm::vec2 c01 = Game::ActorPose::HandWorld(pos, {0.0F, 16.0F}, {0.0F, 16.0F}, -1);
    EXPECT_FLOAT_EQ(c01.x, 100.0F);
    EXPECT_FLOAT_EQ(c01.y, 200.0F);
}

TEST(ActorPoseTest, FeetOffsetXMirrorsWithTheFacingToo) {
    // A body whose collider sits off the root axis: Position() = feet + feetOffset. The img subtree mirrors
    // about the ROOT (feet) axis, so the hand is at feet + (f * hx, hy) = Position() + (f * hx - fx, hy - fy).
    const glm::vec2 hand{8.0F, 10.0F};
    const glm::vec2 feetOff{4.0F, 6.0F};
    const glm::vec2 pos{50.0F, 50.0F};
    const glm::vec2 feet = pos - feetOff;
    EXPECT_FLOAT_EQ(AP::HandWorld(pos, hand, feetOff, 1).x, feet.x + 8.0F);
    EXPECT_FLOAT_EQ(AP::HandWorld(pos, hand, feetOff, -1).x, feet.x - 8.0F);
}

// ---- sprite anchor with the facing mirror ------------------------------------------------------

// Where the drawn sprite's Unity pivot point lands: Util::GameObject draws the quad with model
// T * R * S(scale) and shifts by -pivot/size BEFORE the scale, so the sprite-space point v lands at
// T + scale * (v - pivotParam). The Unity pivot (fractions fx, fy) is v = ((fx-0.5) w, (fy-0.5) h).
glm::vec2 DrawnPivotPoint(const Game::SpriteAnchor &a, glm::vec2 size, std::size_t frame, int facing,
                          glm::vec2 translation) {
    const glm::vec2 param = a.PivotPx(size, frame, facing);
    const float s = a.DrawScale();
    const float sx = facing < 0 ? -s : s;
    std::array<float, 2> pv{0.5F, 0.0F};
    if (frame < a.framePivots.size()) {
        pv = a.framePivots[frame];
    }
    const glm::vec2 v{(pv[0] - 0.5F) * size.x, (pv[1] - 0.5F) * size.y};
    return translation + glm::vec2{sx * (v.x - param.x), s * (v.y - param.y)};
}

TEST(ActorPoseTest, SpriteAnchorPlacesThePivotOnTheFeetWhicheverWayItFaces) {
    Game::SpriteAnchor a;
    a.framePivots = {{0.486F, -0.002F}};
    a.feetOffsetPx = {0.0F, 16.0F};
    const glm::vec2 pos{300.0F, 200.0F};
    const glm::vec2 size{25.0F, 29.0F};
    const glm::vec2 right = DrawnPivotPoint(a, size, 0, 1, pos);
    const glm::vec2 left = DrawnPivotPoint(a, size, 0, -1, pos);
    EXPECT_NEAR(right.x, 300.0F, 1e-3F);
    EXPECT_NEAR(right.y, 184.0F, 1e-3F);
    EXPECT_NEAR(left.x, 300.0F, 1e-3F); // mirrored about the root axis: the feet do not move
    EXPECT_NEAR(left.y, 184.0F, 1e-3F);
    // A sprite GameObject off the root axis (boss05 (0.2, 0.3) units): its x offset mirrors with the facing.
    a.spriteOffsetPx = {6.4F, 9.6F};
    const glm::vec2 r2 = DrawnPivotPoint(a, size, 0, 1, pos);
    const glm::vec2 l2 = DrawnPivotPoint(a, size, 0, -1, pos);
    EXPECT_NEAR(r2.x, 300.0F + 6.4F, 1e-3F);
    EXPECT_NEAR(l2.x, 300.0F - 6.4F, 1e-3F);
    EXPECT_NEAR(r2.y, 184.0F + 9.6F, 1e-3F);
    EXPECT_NEAR(l2.y, 184.0F + 9.6F, 1e-3F);
    // and with an off-axis body collider (feet.x != 0) the feet axis stays where the root is.
    a.spriteOffsetPx = {0.0F, 0.0F};
    a.feetOffsetPx = {4.0F, 16.0F};
    const glm::vec2 r3 = DrawnPivotPoint(a, size, 0, 1, pos);
    const glm::vec2 l3 = DrawnPivotPoint(a, size, 0, -1, pos);
    EXPECT_NEAR(r3.x, 296.0F, 1e-3F);
    EXPECT_NEAR(l3.x, 296.0F, 1e-3F);
}

TEST(ActorPoseTest, ActorVisualPlacesShadowAndRingOnTheRootNotTheMirroredImg) {
    // shadow / shadow_lock are children of the ROOT: feet + offset whatever the facing (no GL needed).
    Game::ActorVisual v;
    Game::SpriteAnchor a;
    a.feetOffsetPx = {0.0F, 16.0F};
    v.SetAnchor(a);
    Game::RigNodeDef shadow;
    shadow.present = true;
    shadow.offsetX = 0.0F;
    shadow.offsetY = -0.05F; // c01 shadow (0, -0.05)
    const glm::vec2 pos{300.0F, 200.0F};
    for (const int f : {1, -1}) {
        v.SetFacing(f);
        const glm::vec2 o = v.RigNodeOrigin(shadow, pos);
        EXPECT_FLOAT_EQ(o.x, 300.0F);
        EXPECT_NEAR(o.y, 200.0F - 16.0F - 1.6F, 1e-4F);
    }
    shadow.offsetX = 0.5F; // an off-axis node must NOT mirror with the facing
    v.SetFacing(-1);
    EXPECT_FLOAT_EQ(v.RigNodeOrigin(shadow, pos).x, 300.0F + 16.0F);
}

TEST(ActorPoseTest, ActorVisualFallsBackToIdleWithoutThatClip) {
    Game::ActorVisual v;
    using C = AP::ActorClip;
    // No clip registered at all (needs no GL): everything resolves to Idle.
    EXPECT_FALSE(v.HasClip(C::Run));
    EXPECT_EQ(v.Resolve(C::Run), C::Idle);
    EXPECT_EQ(v.Resolve(C::Dead), C::Idle);
    v.SetClipState(C::Run);
    EXPECT_EQ(v.Requested(), C::Run);
    EXPECT_EQ(v.Shown(), C::Idle); // a species without a run clip keeps idling
    v.SetFacing(-5);
    EXPECT_EQ(v.Facing(), -1);
    v.SetFacing(0);
    EXPECT_EQ(v.Facing(), 1);
}

// ---- lock target (the red ring) ----------------------------------------------------------------

struct Pt {
    glm::vec2 p;
    bool alive;
};

int Pick(const std::vector<Pt> &pts, glm::vec2 from, float range) {
    return AP::PickLockTarget(
        static_cast<int>(pts.size()), from, range, [&](int i) { return pts[static_cast<std::size_t>(i)].p; },
        [&](int i) { return pts[static_cast<std::size_t>(i)].alive; });
}

TEST(ActorPoseTest, LockPicksTheNearestLivingHostileInRange) {
    const std::vector<Pt> pts{{{300.0F, 0.0F}, true}, {{50.0F, 0.0F}, true}, {{10.0F, 0.0F}, false}, {{-80.0F, 0.0F}, true}};
    EXPECT_EQ(Pick(pts, {0.0F, 0.0F}, 224.0F), 1); // 50 px: nearest living; the dead one at 10 px is ignored
    EXPECT_EQ(Pick(pts, {0.0F, 0.0F}, 40.0F), -1); // nothing within 40 px (the 10 px one is dead)
    EXPECT_EQ(Pick(pts, {-100.0F, 0.0F}, 224.0F), 3);
}

TEST(ActorPoseTest, LockRangeIsInclusiveAndTiesGoToTheLowestIndex) {
    const std::vector<Pt> edge{{{224.0F, 0.0F}, true}};
    EXPECT_EQ(Pick(edge, {0.0F, 0.0F}, 224.0F), 0);       // exactly on the range: locked
    EXPECT_EQ(Pick(edge, {0.0F, 0.0F}, 223.99F), -1);     // a hair outside: not
    const std::vector<Pt> tie{{{0.0F, 100.0F}, true}, {{100.0F, 0.0F}, true}, {{-100.0F, 0.0F}, true}};
    EXPECT_EQ(Pick(tie, {0.0F, 0.0F}, 224.0F), 0);        // three at 100 px: the lowest index
}

TEST(ActorPoseTest, LockRejectsNonFiniteAndEmptyInput) {
    const std::vector<Pt> none;
    EXPECT_EQ(Pick(none, {0.0F, 0.0F}, 224.0F), -1);
    const std::vector<Pt> pts{{{kNaN, 0.0F}, true}, {{kInf, 0.0F}, true}, {{20.0F, 0.0F}, true}};
    EXPECT_EQ(Pick(pts, {0.0F, 0.0F}, 224.0F), 2); // NaN / inf candidates are skipped, never chosen
    EXPECT_EQ(Pick(pts, {kNaN, 0.0F}, 224.0F), -1);
    EXPECT_EQ(Pick(pts, {0.0F, 0.0F}, 0.0F), -1);
    EXPECT_EQ(Pick(pts, {0.0F, 0.0F}, -5.0F), -1);
    EXPECT_EQ(Pick(pts, {0.0F, 0.0F}, kNaN), -1);
    // An overflowing squared distance must not select anything (and must not wrap into range).
    const std::vector<Pt> far{{{1.0e30F, 0.0F}, true}};
    EXPECT_EQ(Pick(far, {0.0F, 0.0F}, 224.0F), -1);
}

} // namespace
