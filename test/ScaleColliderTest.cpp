// Character/enemy/boss sprite scale + prefab-collider data (the "knight is drawn too
// small" fix). Verifies: the shared scale constant, the collider numbers extracted from
// the prefabs (c01 / e_slime01 / e_yeti01 / boss01 / a bullet), the feet-anchored sprite
// pivot math, BodyGeom's px conversion and hit boundary, and that the sim hits an enemy
// exactly at the data radius boundary.
#include <gtest/gtest.h>

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
        Game::MakeSpriteAnchor(data, "c01", c->body, c->hurtbox, c->bodyScale, 16.0F);
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

TEST(SpriteAnchorTest, NoDataAnchorsBottomCentre) {
    Game::SpriteAnchor a;
    a.feetOffsetPx = {0.0F, 0.0F};
    const glm::vec2 p = a.PivotPx({20.0F, 30.0F}, 3); // no frame data: {0.5, 0}
    EXPECT_FLOAT_EQ(p.x, 0.0F);
    EXPECT_FLOAT_EQ(p.y, -15.0F);
}

// ---- (c) sim: a bullet hits an enemy exactly at the data radius boundary -------------
namespace {
Game::Sim::WorldInputs FireAlongX() {
    Game::Sim::WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.firing = true;
    in.playerRoomId = 1; // not the enemy's room: it stays asleep and does not move
    return in;
}

// Fire straight along +x past a slime whose hurtbox centre is @p enemyY above the line of
// fire; true if its hp dropped.
bool SlimeHit(const Game::GameData &gd, const Game::WeaponDef &w, float enemyY) {
    const Game::EnemyDef *slime = gd.FindEnemy("e_slime01");
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    // Position() is the BODY centre; the slime's hurtbox centre coincides with it
    // (both offsets 0.5 unit), so enemyY is also the hurtbox centre.
    sim.AddEnemy(*slime, glm::vec2{120.0F, enemyY}, /*roomId=*/0, 21000);
    sim.EquipWeapon(w, "Gun001", 5, &gd);
    const int hp0 = sim.EnemyViews()[0].hp;
    const Game::Sim::WorldInputs in = FireAlongX();
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
    // The OLD placeholder geometry (bullet 4 + circle 16 = 20 px) would have hit at 21.9 and
    // 22.9 alike: the data radius is what separates these two.
    EXPECT_GT(half + bulletR + 0.5F, 20.0F);
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
        const Game::Sim::WorldInputs in = FireAlongX();
        for (int i = 0; i < 60; ++i) {
            sim.Advance(20.0F, in);
        }
        return sim.EnemyViews()[0].hp < hp0;
    };
    EXPECT_TRUE(hit(Game::Sim::kBulletRadius + Game::Sim::kEnemyBodyRadius - 0.5F));
    EXPECT_FALSE(hit(Game::Sim::kBulletRadius + Game::Sim::kEnemyBodyRadius + 0.5F));
}

// NOLINTEND(readability-magic-numbers)
