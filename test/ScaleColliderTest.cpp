// Character/enemy/boss sprite scale + prefab-collider data (the "knight is drawn too
// small" fix). Verifies: the shared scale constant, the collider numbers extracted from
// the prefabs (c01 / e_slime01 / e_yeti01 / boss01 / a bullet), the feet-anchored sprite
// pivot math, BodyGeom's px conversion and hit boundary, and that the sim hits an enemy
// exactly at the data radius boundary.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "entities/SpriteAnchor.hpp"
#include "game/WorldScale.hpp"
#include "sim/BodyGeom.hpp"
#include "sim/SimConfig.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::ColliderDef;
using Game::Sim::BodyGeom;
using Game::Sim::MakeBodyGeom;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
const char *kResourceRoot = RESOURCE_DIR; // CMake-injected, as in GameDataTest.
Game::Sim::NullWorldCollision g_NullWorld;

ColliderDef Circle(float r, float ox, float oy) {
    ColliderDef c;
    c.shape = ColliderDef::Shape::Circle;
    c.radius = r;
    c.offsetX = ox;
    c.offsetY = oy;
    return c;
}
ColliderDef Box(float w, float h, float ox, float oy) {
    ColliderDef c;
    c.shape = ColliderDef::Shape::Box;
    c.sizeX = w;
    c.sizeY = h;
    c.offsetX = ox;
    c.offsetY = oy;
    c.trigger = true;
    return c;
}
} // namespace

// ---- (a) the scale rule ------------------------------------------------------------
TEST(ScaleTest, SpriteScaleIsCellPxOver16) {
    EXPECT_EQ(Game::Scale::kSpriteScale, Game::Scale::kCellPx / 16.0F);
    EXPECT_EQ(Game::Scale::kSpriteScale, 2.0F);
    EXPECT_EQ(Game::Scale::kCellPx, 32.0F);
    EXPECT_EQ(Game::Scale::kSourcePxPerUnit, 16.0F);
    // The sim's unit conversion and the world tiles use the same cell size.
    EXPECT_EQ(Game::Sim::kWorldUnitPx, Game::Scale::kCellPx);
    EXPECT_EQ(Game::Scale::UnitsToPx(0.4F), 0.4F * 32.0F);
}

TEST(ScaleTest, KnightSpriteCoversAboutOnePointSixByOnePointEightCells) {
    // c01_4.png is 25x29 source px: 25/16 x 29/16 units = 1.56 x 1.81 cells (the original),
    // i.e. 50 x 58 world px at kSpriteScale (was 25 x 29 = 0.8 x 0.9 cells at scale 1).
    const float s = Game::Scale::kSpriteScale;
    EXPECT_NEAR(25.0F * s / Game::Scale::kCellPx, 1.5625F, 1e-4F);
    EXPECT_NEAR(29.0F * s / Game::Scale::kCellPx, 1.8125F, 1e-4F);
}

// ---- (b) collider numbers == the prefab numbers ------------------------------------
class ColliderDataTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() { ASSERT_TRUE(s_Data.LoadAll(kResourceRoot)); }
    static Game::GameData s_Data;
};
Game::GameData ColliderDataTest::s_Data;

TEST_F(ColliderDataTest, KnightC01MatchesPrefab) {
    // res/player/c01.prefab: root BoxCollider2D (IsTrigger 1) size 0.5x0.5 offset (0, 0.7);
    // child "collider" CircleCollider2D (IsTrigger 0) radius 0.4 offset (0, 0.5).
    const Game::CharacterDef *c = s_Data.FindCharacter("c01");
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->body.shape, ColliderDef::Shape::Circle);
    EXPECT_FLOAT_EQ(c->body.radius, 0.4F);
    EXPECT_FLOAT_EQ(c->body.offsetX, 0.0F);
    EXPECT_FLOAT_EQ(c->body.offsetY, 0.5F);
    EXPECT_FALSE(c->body.trigger);
    ASSERT_EQ(c->hurtbox.shape, ColliderDef::Shape::Box);
    EXPECT_FLOAT_EQ(c->hurtbox.sizeX, 0.5F);
    EXPECT_FLOAT_EQ(c->hurtbox.sizeY, 0.5F);
    EXPECT_FLOAT_EQ(c->hurtbox.offsetY, 0.7F);
    EXPECT_TRUE(c->hurtbox.trigger);
    EXPECT_FLOAT_EQ(c->bodyScale, 1.0F);
}

TEST_F(ColliderDataTest, SlimeAndYetiMatchPrefab) {
    // res/enemy/e_slime01.prefab and 1-ice/e_yeti01.prefab: hurtbox Box 1x1 @ (0, 0.5),
    // body Circle r 0.4 @ (0, 0.5); img/body localScale 1.
    for (const char *id : {"e_slime01", "e_yeti01"}) {
        const Game::EnemyDef *e = s_Data.FindEnemy(id);
        ASSERT_NE(e, nullptr) << id;
        ASSERT_EQ(e->body.shape, ColliderDef::Shape::Circle) << id;
        EXPECT_FLOAT_EQ(e->body.radius, 0.4F) << id;
        EXPECT_FLOAT_EQ(e->body.offsetY, 0.5F) << id;
        ASSERT_EQ(e->hurtbox.shape, ColliderDef::Shape::Box) << id;
        EXPECT_FLOAT_EQ(e->hurtbox.sizeX, 1.0F) << id;
        EXPECT_FLOAT_EQ(e->hurtbox.sizeY, 1.0F) << id;
        EXPECT_FLOAT_EQ(e->hurtbox.offsetY, 0.5F) << id;
        EXPECT_TRUE(e->hurtbox.trigger) << id;
        EXPECT_FLOAT_EQ(e->bodyScale, 1.0F) << id;
    }
}

TEST_F(ColliderDataTest, Boss01MatchesPrefab) {
    // res/enemy/2-castle/boss01.prefab: hurtbox Box 1.2x1.8 @ (0, 1.3), body Circle r 0.6
    // @ (0, 1.0). img/body is localScale 1 (the 1.5 is only on shadow/shadow_lock/point_1).
    const Game::EnemyDef *b = s_Data.FindEnemy("boss01");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->body.shape, ColliderDef::Shape::Circle);
    EXPECT_FLOAT_EQ(b->body.radius, 0.6F);
    EXPECT_FLOAT_EQ(b->body.offsetY, 1.0F);
    ASSERT_EQ(b->hurtbox.shape, ColliderDef::Shape::Box);
    EXPECT_FLOAT_EQ(b->hurtbox.sizeX, 1.2F);
    EXPECT_FLOAT_EQ(b->hurtbox.sizeY, 1.8F);
    EXPECT_FLOAT_EQ(b->hurtbox.offsetY, 1.3F);
    EXPECT_FLOAT_EQ(b->bodyScale, 1.0F);
}

TEST_F(ColliderDataTest, BodyScaleVariesOnlyWhereThePrefabSaysSo) {
    // e_shark/img/body 1.5; ex_slime01/img/body 2.0 (elite).
    EXPECT_FLOAT_EQ(s_Data.FindEnemy("e_shark")->bodyScale, 1.5F);
    EXPECT_FLOAT_EQ(s_Data.FindEnemy("ex_slime01")->bodyScale, 2.0F);
    // No boss prefab scales its body.
    for (const Game::EnemyDef &e : s_Data.Enemies()) {
        if (e.id.rfind("boss", 0) == 0) {
            EXPECT_FLOAT_EQ(e.bodyScale, 1.0F) << e.id;
        }
    }
}

TEST_F(ColliderDataTest, BulletZeroMatchesPrefab) {
    // rgprefab/bullet/bullet_0.prefab: BoxCollider2D 0.5x0.4 @ (0.25, 0), IsTrigger.
    const Game::BulletDef *b = s_Data.FindBullet("bullet_0");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->collider.shape, ColliderDef::Shape::Box);
    EXPECT_FLOAT_EQ(b->collider.sizeX, 0.5F);
    EXPECT_FLOAT_EQ(b->collider.sizeY, 0.4F);
    EXPECT_FLOAT_EQ(b->collider.offsetX, 0.25F);
    EXPECT_TRUE(b->collider.trigger);
    // Equivalent circle: the box's inscribed circle, 0.2 unit = 6.4 px.
    EXPECT_FLOAT_EQ(Game::Sim::EquivalentRadiusPx(b->collider), 6.4F);
}

TEST_F(ColliderDataTest, EveryHeroAndEnemyCarriesColliders) {
    ASSERT_EQ(s_Data.Characters().size(), 13U);
    for (const Game::CharacterDef &c : s_Data.Characters()) {
        EXPECT_TRUE(c.body.Present()) << c.id;
        EXPECT_TRUE(c.hurtbox.Present()) << c.id;
    }
    for (const Game::EnemyDef &e : s_Data.Enemies()) {
        EXPECT_TRUE(e.hurtbox.Present()) << e.id;
        if (e.id != "e_snowman_temp") { // the one prefab with no physical "collider" child
            EXPECT_TRUE(e.body.Present()) << e.id;
        }
    }
}

// HIERARCHY (R27): collider centres and the sprite GameObject are placed in the prefab
// ROOT's space -- every ancestor's localPosition + localScale * q is composed, not just
// the collider's own m_Offset / the body's own scale.
TEST_F(ColliderDataTest, MineralColliderChildPositionIsComposedIntoRootSpace) {
    // res/enemy/mineral_gold.prefab: child "collider" at localPosition (0, 0.8) carrying a
    // CircleCollider2D r 0.8 with m_Offset (0, 0.5) -> root-space centre y = 0.8 + 0.5 = 1.3
    // (the data used to say 0.5). The root's own hurtbox BoxCollider2D is at offset (0, 0.8).
    for (const char *id : {"mineral_gold", "mineral_crystal"}) {
        const Game::EnemyDef *m = s_Data.FindEnemy(id);
        ASSERT_NE(m, nullptr) << id;
        ASSERT_EQ(m->body.shape, ColliderDef::Shape::Circle) << id;
        EXPECT_FLOAT_EQ(m->body.radius, 0.8F) << id;
        EXPECT_FLOAT_EQ(m->body.offsetX, 0.0F) << id;
        EXPECT_NEAR(m->body.offsetY, 1.3F, 1e-5F) << id;
        ASSERT_EQ(m->hurtbox.shape, ColliderDef::Shape::Box) << id;
        EXPECT_NEAR(m->hurtbox.offsetY, 0.8F, 1e-5F) << id;
    }
    // BodyGeom: feet 1.3 * 32 = 41.6 px below the body centre; the hurtbox centre sits
    // 0.5 unit (16 px) BELOW the body centre.
    const Game::EnemyDef *m = s_Data.FindEnemy("mineral_gold");
    const BodyGeom g = MakeBodyGeom(m->body, m->hurtbox, 16.0F);
    EXPECT_NEAR(g.feetOffsetPx.y, 41.6F, 1e-3F);
    EXPECT_NEAR(g.hurtOffsetPx.y, -16.0F, 1e-3F);
}

namespace {
// World position (relative to Position()) of frame @p frame's pivot point once drawn the
// way Util::GameObject::Draw does: quad centre = translation - pivot * scale.
glm::vec2 DrawnPivotPoint(const Game::SpriteAnchor &a, glm::vec2 size, std::size_t frame) {
    const glm::vec2 pivot = a.PivotPx(size, frame);
    const float s = a.DrawScale();
    const glm::vec2 centre = -pivot * s;
    std::array<float, 2> pv{0.5F, 0.0F};
    if (frame < a.framePivots.size()) {
        pv = a.framePivots[frame];
    }
    return centre + glm::vec2{(pv[0] - 0.5F) * size.x, (pv[1] - 0.5F) * size.y} * s;
}

Game::SpriteAnchor AnchorOf(const Game::GameData &gd, const char *id, bool withSpriteOffset) {
    const Game::EnemyDef *e = gd.FindEnemy(id);
    const glm::vec2 off = withSpriteOffset ? glm::vec2{e->spriteOffsetX, e->spriteOffsetY}
                                           : glm::vec2{0.0F, 0.0F};
    return Game::MakeSpriteAnchor(gd, id, e->body, e->hurtbox, e->bodyScale, off, 24.0F);
}
} // namespace

TEST_F(ColliderDataTest, Boss11SpriteOffsetAndDrawPosition) {
    // res/enemy/3-volcano/boss11.prefab: img (0,0) -> body at localPosition (-0.074, -1.1):
    // the sprite pivot is 1.1 units (35.2 px) BELOW the root origin, 0.074 units left.
    const Game::EnemyDef *b = s_Data.FindEnemy("boss11");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->spriteOffsetX, -0.074F, 1e-5F);
    EXPECT_NEAR(b->spriteOffsetY, -1.1F, 1e-5F);
    EXPECT_FLOAT_EQ(b->body.offsetY, 1.0F); // body circle centre 1 unit above the root
    const glm::vec2 size{40.0F, 50.0F};
    const Game::SpriteAnchor with = AnchorOf(s_Data, "boss11", true);
    const Game::SpriteAnchor without = AnchorOf(s_Data, "boss11", false);
    ASSERT_FALSE(with.framePivots.empty());
    EXPECT_NEAR(with.spriteOffsetPx.x, -0.074F * 32.0F, 1e-3F);
    EXPECT_NEAR(with.spriteOffsetPx.y, -35.2F, 1e-3F);
    // Pivot lands at root origin + sprite_offset: Position() - 32 - 35.2 px, x -2.368 px.
    const glm::vec2 p = DrawnPivotPoint(with, size, 0);
    EXPECT_NEAR(p.x, -2.368F, 1e-3F);
    EXPECT_NEAR(p.y, -67.2F, 1e-3F);
    // Ignoring the hierarchy (the R27 bug) drew the sprite 35.2 px too high.
    const glm::vec2 q = DrawnPivotPoint(without, size, 0);
    EXPECT_NEAR(q.y, -32.0F, 1e-3F);
    EXPECT_NEAR(p.y - q.y, -35.2F, 1e-3F);
}

TEST_F(ColliderDataTest, Boss05SpriteOffset) {
    // res/enemy/3-alien/boss05.prefab: img (0,0) -> body at localPosition (0.2, 0.3);
    // hurtbox Box @ (0, 1), body Circle r 0.6 @ (0, 1) (root children at the origin).
    const Game::EnemyDef *b = s_Data.FindEnemy("boss05");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->spriteOffsetX, 0.2F, 1e-5F);
    EXPECT_NEAR(b->spriteOffsetY, 0.3F, 1e-5F);
    EXPECT_FLOAT_EQ(b->body.offsetY, 1.0F);
    const glm::vec2 size{60.0F, 60.0F};
    const Game::SpriteAnchor a = AnchorOf(s_Data, "boss05", true);
    const Game::SpriteAnchor flat = AnchorOf(s_Data, "boss05", false);
    const glm::vec2 p = DrawnPivotPoint(a, size, 0);
    EXPECT_NEAR(p.x, 6.4F, 1e-3F);          // 0.2 unit right of the root origin
    EXPECT_NEAR(p.y, -32.0F + 9.6F, 1e-3F); // feet 32 px below the body, +0.3 unit up
    const glm::vec2 q = DrawnPivotPoint(flat, size, 0);
    EXPECT_NEAR(p.x - q.x, 6.4F, 1e-3F);
    EXPECT_NEAR(p.y - q.y, 9.6F, 1e-3F);
}

TEST_F(ColliderDataTest, SpriteOffsetIsZeroExceptForTheSurveyedPrefabs) {
    // Survey of every hero/enemy/boss prefab (tools/build_gamedata.py): only these have a
    // sprite GameObject displaced from the root (units, root space).
    for (const Game::CharacterDef &c : s_Data.Characters()) {
        EXPECT_FLOAT_EQ(c.spriteOffsetX, 0.0F) << c.id;
        EXPECT_FLOAT_EQ(c.spriteOffsetY, 0.0F) << c.id;
    }
    struct Row {
        const char *id;
        float x;
        float y;
    };
    const std::vector<Row> expected = {
        {"boss05", 0.2F, 0.3F},          {"boss11", -0.074F, -1.1F},
        {"boss12_1", 0.0F, -0.2F},       {"boss12_2", 0.0F, -0.2F},
        {"e_creeper_fly", 0.0F, -0.7F},  {"e_mosquito", 0.0F, -0.2F},
        {"e_ufo", 0.0F, -0.7F},          {"ex_creeper_fly", 0.0F, -1.1F},
        {"ex_mosquito", 0.0F, -0.35F},   {"ex_ufo", 0.0F, -1.1F},
    };
    int nonZero = 0;
    for (const Game::EnemyDef &e : s_Data.Enemies()) {
        if (e.spriteOffsetX != 0.0F || e.spriteOffsetY != 0.0F) {
            ++nonZero;
        }
        float ex = 0.0F;
        float ey = 0.0F;
        for (const Row &r : expected) {
            if (e.id == r.id) {
                ex = r.x;
                ey = r.y;
            }
        }
        EXPECT_NEAR(e.spriteOffsetX, ex, 1e-5F) << e.id;
        EXPECT_NEAR(e.spriteOffsetY, ey, 1e-5F) << e.id;
    }
    EXPECT_EQ(nonZero, static_cast<int>(expected.size()));
}

// ---- BodyGeom: units -> px, feet offset, hit boundary --------------------------------
TEST(BodyGeomTest, KnightGeometryInPixels) {
    const BodyGeom g = MakeBodyGeom(Circle(0.4F, 0.0F, 0.5F), Box(0.5F, 0.5F, 0.0F, 0.7F), 16.0F);
    EXPECT_TRUE(g.fromData);
    EXPECT_FLOAT_EQ(g.bodyRadiusPx, 12.8F);
    EXPECT_FLOAT_EQ(g.feetOffsetPx.y, 16.0F); // feet 16 px below the body centre
    EXPECT_TRUE(g.hurtIsBox);
    EXPECT_FLOAT_EQ(g.hurtHalfPx.x, 8.0F);
    EXPECT_FLOAT_EQ(g.hurtHalfPx.y, 8.0F);
    EXPECT_FLOAT_EQ(g.hurtOffsetPx.y, 6.4F); // 0.7 - 0.5 unit above the body centre
}

TEST(BodyGeomTest, MissingDataFallsBackToThePlaceholderRadius) {
    const BodyGeom g = MakeBodyGeom(ColliderDef{}, ColliderDef{}, 24.0F);
    EXPECT_FALSE(g.fromData);
    EXPECT_FLOAT_EQ(g.bodyRadiusPx, 24.0F);
    EXPECT_FLOAT_EQ(g.hurtRadiusPx, 24.0F);
    EXPECT_FALSE(g.hurtIsBox);
}

TEST(BodyGeomTest, HurtboxHitBoundaryIsInclusive) {
    // Slime: Box 1x1 -> half 16 px, centred on the body (equal offsets).
    const BodyGeom box = MakeBodyGeom(Circle(0.4F, 0, 0.5F), Box(1, 1, 0, 0.5F), 16.0F);
    const glm::vec2 pos{0.0F, 0.0F};
    const float r = 6.4F;
    EXPECT_TRUE(box.HurtOverlaps(pos, {16.0F + r, 0.0F}, r));         // tangent to the right edge
    EXPECT_FALSE(box.HurtOverlaps(pos, {16.0F + r + 0.01F, 0.0F}, r)); // 0.01 px outside
    EXPECT_TRUE(box.HurtOverlaps(pos, {0.0F, -(16.0F + r)}, r));
    // Corner: nearest point is the corner (16,16): distance sqrt(2)*(d) from it.
    EXPECT_TRUE(box.HurtOverlaps(pos, {16.0F + r * 0.7F, 16.0F + r * 0.7F}, r));
    EXPECT_FALSE(box.HurtOverlaps(pos, {16.0F + r * 0.8F, 16.0F + r * 0.8F}, r));
    // A circle hurtbox keeps the old circle-circle rule.
    const BodyGeom circ = BodyGeom::Fallback(16.0F);
    EXPECT_TRUE(circ.HurtOverlaps(pos, {16.0F + r, 0.0F}, r));
    EXPECT_FALSE(circ.HurtOverlaps(pos, {16.0F + r + 0.01F, 0.0F}, r));
}

// ---- pivot: feet land on Position() - feetOffset -----------------------------------
TEST(SpriteAnchorTest, KnightFeetAlignWithPrefabOrigin) {
    Game::GameData data;
    ASSERT_TRUE(data.LoadAll(kResourceRoot));
    const Game::CharacterDef *c = data.FindCharacter("c01");
    ASSERT_NE(c, nullptr);
    const Game::SpriteAnchor a =
        Game::MakeSpriteAnchor(data, "c01", c->body, c->hurtbox, c->bodyScale,
                               {c->spriteOffsetX, c->spriteOffsetY}, 16.0F);
    ASSERT_FALSE(a.framePivots.empty());
    EXPECT_FLOAT_EQ(a.DrawScale(), 2.0F);
    EXPECT_FLOAT_EQ(a.feetOffsetPx.y, 16.0F);
    // c01_4 (idle frame 0): 25x29 source px, pivot ~ bottom centre (0.486, -0.002).
    const glm::vec2 size{25.0F, 29.0F};
    const glm::vec2 pivot = a.PivotPx(size, 0);
    const float s = a.DrawScale();
    const glm::vec2 centre = -pivot * s; // where Draw() puts the quad centre (pos = 0)
    const auto &pv = a.framePivots[0];
    const glm::vec2 pivotWorld =
        centre + glm::vec2{(pv[0] - 0.5F) * size.x, (pv[1] - 0.5F) * size.y} * s;
    EXPECT_NEAR(pivotWorld.x, 0.0F, 1e-3F);
    EXPECT_NEAR(pivotWorld.y, -16.0F, 1e-3F); // the feet, 16 px below the body centre
    // And the sprite sits on them: its bottom edge is within a pixel of the feet.
    EXPECT_NEAR(centre.y - size.y * s * 0.5F, -16.0F, 0.2F * s);
}

TEST(SpriteAnchorTest, SpriteOffsetMovesThePivotPointInRootSpace) {
    Game::SpriteAnchor a;
    a.feetOffsetPx = {0.0F, 32.0F};
    a.spriteOffsetPx = {-2.368F, -35.2F};
    a.framePivots = {{0.5F, 0.0F}};
    const glm::vec2 size{20.0F, 30.0F};
    const glm::vec2 centre = -a.PivotPx(size, 0) * a.DrawScale();
    const glm::vec2 pivotWorld = centre + glm::vec2{0.0F, -15.0F} * a.DrawScale();
    EXPECT_NEAR(pivotWorld.x, -2.368F, 1e-3F);
    EXPECT_NEAR(pivotWorld.y, -32.0F - 35.2F, 1e-3F);
}

TEST(SpriteAnchorTest, NoDataAnchorsBottomCentre) {
    Game::SpriteAnchor a;
    a.feetOffsetPx = {0.0F, 0.0F};
    const glm::vec2 p = a.PivotPx({20.0F, 30.0F}, 3); // no frame data: {0.5, 0}
    EXPECT_FLOAT_EQ(p.x, 0.0F);
    EXPECT_FLOAT_EQ(p.y, -15.0F);
}

// ---- (c) sim: a bullet hits an enemy exactly at the data radius boundary -------------
namespace {
// SLICE 4: the shot leaves the weapon's gun_point, not the body centre. FireAlongX stands the player so the
// MUZZLE (hand + gun_point at aim +x, facing +1) lands on the old origin (0, 0): every geometry below (enemy
// offsets from the line of fire, the 6.4 px bullet, the boundary values) keeps its pre-slice meaning, and the
// expectations are unchanged. The muzzle is the data value (weapons.json held.gun_point * kCellPx).
glm::vec2 MuzzleOffsetPx(const Game::WeaponDef &w) {
    return w.held.hasGunPoint ? glm::vec2{Game::Scale::UnitsToPx(w.held.gunPointX),
                                          Game::Scale::UnitsToPx(w.held.gunPointY)}
                              : glm::vec2{0.0F, 0.0F};
}

Game::Sim::WorldInputs FireAlongX(const Game::WeaponDef &w) {
    Game::Sim::WorldInputs in;
    in.playerPos = -MuzzleOffsetPx(w);
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.firing = true;
    in.playerRoomId = 1; // not the enemy's room: it stays asleep and does not move
    return in;
}

// T-57: the boundary probes fire at the REAL weapon speed (weapon_000: 36 units/s = 1152 px/s =
// 23 px per fixed step), and `bulletUnitsPerSec` > 0 overrides it (the 64 units/s fastest-weapon
// probe). P3.0 had to slow these probes to 5 units/s because the hit test was a discrete overlap
// once per step: a glancing 0.5 px-deep overlap (a chord of ~9 px) is sampled by only some of
// the steps, so the probe tested step aliasing, not the radius. The swept (continuous) test
// reports that chord at ANY speed, so the probes are deterministic at the real speed again.

// Fire straight along +x past a slime whose hurtbox centre is @p enemyY above the line of
// fire; true if its hp dropped.
bool SlimeHit(const Game::GameData &gd, Game::WeaponDef w, float enemyY, bool stripData = false,
              float bulletUnitsPerSec = 0.0F) {
    Game::EnemyDef slimeCopy = *gd.FindEnemy("e_slime01");
    if (bulletUnitsPerSec > 0.0F) {
        w.bulletSpeed = bulletUnitsPerSec;
    }
    if (stripData) { // the old placeholder geometry: 16 px circle + 4 px bullet
        slimeCopy.body = ColliderDef{};
        slimeCopy.hurtbox = ColliderDef{};
        w.bulletPrefab.clear();
    }
    const Game::EnemyDef *slime = &slimeCopy;
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    // Position() is the BODY centre; the slime's hurtbox centre coincides with it
    // (both offsets 0.5 unit), so enemyY is also the hurtbox centre.
    sim.AddEnemy(*slime, glm::vec2{120.0F, enemyY}, /*roomId=*/0, 21000);
    sim.EquipWeapon(w, "Gun001", 5, &gd);
    const int hp0 = sim.EnemyViews()[0].hp;
    const Game::Sim::WorldInputs in = FireAlongX(w);
    for (int i = 0; i < 60; ++i) {
        sim.Advance(20.0F, in);
    }
    return sim.EnemyViews()[0].hp < hp0;
}
} // namespace

TEST(SimColliderTest, BulletHitsEnemyAtTheDataRadiusBoundary) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    Game::WeaponDef w = *gd.FindWeapon("Gun001"); // class fallback -> weapon_000 / bullet_0
    w.deviation = 0;                              // dead straight: no scatter cone
    const Game::BulletDef *bd = gd.BulletForWeapon(w);
    ASSERT_NE(bd, nullptr);
    const float bulletR = Game::Sim::EquivalentRadiusPx(bd->collider);
    ASSERT_FLOAT_EQ(bulletR, 6.4F);
    const float half = 16.0F; // slime hurtbox: 1x1 unit box -> half extent 16 px
    // The bullet flies along y = 0. The hurtbox's bottom edge is at enemyY - 16, so the
    // bullet (radius 6.4) touches it at enemyY = 16 + 6.4.
    EXPECT_TRUE(SlimeHit(gd, w, half + bulletR - 0.5F));  // 0.5 px inside the boundary: hit
    EXPECT_FALSE(SlimeHit(gd, w, half + bulletR + 0.5F)); // 0.5 px outside: clean miss
    // The OLD placeholder geometry (bullet 4 + circle 16 = 20 px) MISSES at both 21.9 and 22.9
    // (both are beyond 20): only the data radius makes 21.9 a hit. Proved below by running
    // the same shot with the colliders and the bullet prefab stripped.
    EXPECT_GT(half + bulletR - 0.5F, Game::Sim::kBulletRadius + Game::Sim::kEnemyBodyRadius);
    EXPECT_FALSE(SlimeHit(gd, w, half + bulletR - 0.5F, /*stripData=*/true));
    EXPECT_FALSE(SlimeHit(gd, w, half + bulletR + 0.5F, /*stripData=*/true));
    // The same boundary at the fastest data speed (64 units/s = 41 px per step) and a slow one.
    for (const float speed : {5.0F, 64.0F}) {
        EXPECT_TRUE(SlimeHit(gd, w, half + bulletR - 0.5F, false, speed)) << speed;
        EXPECT_FALSE(SlimeHit(gd, w, half + bulletR + 0.5F, false, speed)) << speed;
    }
}

TEST(SimColliderTest, DefWithoutColliderDataKeepsPlaceholderRadii) {
    // A bare EnemyDef{} (no prefab data) hits at the old kBulletRadius + kEnemyBodyRadius = 20.
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::WeaponDef *w0 = gd.FindWeapon("Gun001");
    ASSERT_NE(w0, nullptr);
    Game::WeaponDef w = *w0;
    w.deviation = 0;
    w.bulletPrefab.clear(); // no bullet prefab -> fallback bullet radius (4 px)
    auto hit = [&](float y) {
        Game::Sim::Simulation sim(20240607, &g_NullWorld);
        Game::EnemyDef bare;
        sim.AddEnemy(bare, glm::vec2{120.0F, y}, 0, 21000);
        sim.EquipWeapon(w, "Gun001", 5, &gd);
        const int hp0 = sim.EnemyViews()[0].hp;
        const Game::Sim::WorldInputs in = FireAlongX(w);
        for (int i = 0; i < 60; ++i) {
            sim.Advance(20.0F, in);
        }
        return sim.EnemyViews()[0].hp < hp0;
    };
    EXPECT_TRUE(hit(Game::Sim::kBulletRadius + Game::Sim::kEnemyBodyRadius - 0.5F));
    EXPECT_FALSE(hit(Game::Sim::kBulletRadius + Game::Sim::kEnemyBodyRadius + 0.5F));
}

// NOLINTEND(readability-magic-numbers)
