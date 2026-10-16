#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "game/WorldScale.hpp"
#include "sim/SimConfig.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

// Slice 4 (characters): the player's bullets leave the weapon's gun_point (hand mount h1 + the weapon pose),
// not the body centre. FAITHFUL @ weapon_000.prefab (w at (0,0), gun_point (0.6, 0.15) under it) and
// c01.prefab (h1 (0, 0.5) under img), Gun001$$Attack DISASM 0x3eb124 (PrefabPool.Take at gun_point.position).
// Every expectation below is derived by hand from those two numbers, not from the code under test.
namespace {

Game::Sim::NullWorldCollision g_NullWorld;
const char *kResourceRoot = RESOURCE_DIR;
constexpr float kGx = 19.2F; // 0.6 unit * 32 px
constexpr float kGy = 4.8F;  // 0.15 unit * 32 px
constexpr float kTol = 2e-3F;

Game::Sim::WorldInputs Fire(glm::vec2 aim, glm::vec2 pos = {0.0F, 0.0F}) {
    Game::Sim::WorldInputs in;
    in.playerPos = pos;
    in.aimDir = aim;
    in.playerRoomId = 0;
    in.firing = true;
    return in;
}

// The spawn point of the first bullet of a fresh weapon_000 sim after one step (prevPos = the step start,
// which for a bullet spawned in that step is the muzzle).
struct First {
    glm::vec2 spawn;
    glm::vec2 vel;
    int facing;
    glm::vec2 muzzleApi;
};

First FireOnce(const Game::GameData &gd, const std::vector<glm::vec2> &priorAims, glm::vec2 aim,
               glm::vec2 pos = {0.0F, 0.0F}) {
    Game::WeaponDef w = *gd.FindWeapon("weapon_000");
    w.deviation = 0;
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    sim.EquipWeapon(w, "Gun001", 5, &gd);
    // c01: hand h1 (0, 0.5) units, body centre 16 px above the feet -> the hand is the body centre.
    sim.SetPlayerHand({0.0F, 16.0F}, {0.0F, 16.0F});
    for (const glm::vec2 &p : priorAims) { // set the facing history without firing
        Game::Sim::WorldInputs in = Fire(p, pos);
        in.firing = false;
        sim.Advance(20.0F, in);
    }
    sim.Advance(20.0F, Fire(aim, pos));
    First f{};
    EXPECT_EQ(sim.Bullets().size(), 1U);
    if (!sim.Bullets().empty()) {
        f.spawn = sim.Bullets()[0].prevPos;
        f.vel = sim.Bullets()[0].vel;
    }
    f.facing = sim.PlayerFacing();
    f.muzzleApi = sim.PlayerMuzzle(pos, aim, f.facing);
    return f;
}

TEST(SimMuzzleTest, Weapon000DataIsTheGunPointOfThePrefab) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::WeaponDef *w = gd.FindWeapon("weapon_000");
    ASSERT_NE(w, nullptr);
    ASSERT_TRUE(w->held.present);
    ASSERT_TRUE(w->held.hasGunPoint);
    EXPECT_FLOAT_EQ(w->held.gunPointX, 0.6F);
    EXPECT_FLOAT_EQ(w->held.gunPointY, 0.15F);
    EXPECT_EQ(w->held.sprite.sprite, "weapons_19.png");
    EXPECT_FLOAT_EQ(w->held.sprite.pivotX, 0.0F);
    EXPECT_FLOAT_EQ(w->held.sprite.pivotY, 0.35F);
    EXPECT_FLOAT_EQ(w->held.sprite.offsetX, 0.0F);
    EXPECT_FLOAT_EQ(w->held.sprite.offsetY, 0.0F);
}

TEST(SimMuzzleTest, BulletSpawnsAtTheGunPointAt0_90_180_270Degrees) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    // Fresh sim -> facing +1: R(a) * (19.2, 4.8).
    const First a0 = FireOnce(gd, {}, {1.0F, 0.0F});
    EXPECT_EQ(a0.facing, 1);
    EXPECT_NEAR(a0.spawn.x, kGx, kTol);
    EXPECT_NEAR(a0.spawn.y, kGy, kTol);
    const First a90 = FireOnce(gd, {}, {0.0F, 1.0F});
    EXPECT_EQ(a90.facing, 1); // a vertical aim has no side: keeps the initial +1
    EXPECT_NEAR(a90.spawn.x, -kGy, kTol);
    EXPECT_NEAR(a90.spawn.y, kGx, kTol);
    // Aiming left flips the facing to -1 and the muzzle stays on the TOP of the barrel: (-19.2, +4.8).
    const First a180 = FireOnce(gd, {}, {-1.0F, 0.0F});
    EXPECT_EQ(a180.facing, -1);
    EXPECT_NEAR(a180.spawn.x, -kGx, kTol);
    EXPECT_NEAR(a180.spawn.y, kGy, kTol);
    // 270 from a fresh sim (facing +1) ...
    const First a270 = FireOnce(gd, {}, {0.0F, -1.0F});
    EXPECT_EQ(a270.facing, 1);
    EXPECT_NEAR(a270.spawn.x, kGy, kTol);
    EXPECT_NEAR(a270.spawn.y, -kGx, kTol);
    // ... and after the hero last faced left (the facing is kept): R(270) * (19.2, -4.8) = (-4.8, -19.2).
    const First a270L = FireOnce(gd, {{-1.0F, 0.0F}}, {0.0F, -1.0F});
    EXPECT_EQ(a270L.facing, -1);
    EXPECT_NEAR(a270L.spawn.x, -kGy, kTol);
    EXPECT_NEAR(a270L.spawn.y, -kGx, kTol);
    // The public PlayerMuzzle() reports the very same point the bullet spawned at.
    for (const First *f : {&a0, &a90, &a180, &a270, &a270L}) {
        EXPECT_NEAR(f->muzzleApi.x, f->spawn.x, kTol);
        EXPECT_NEAR(f->muzzleApi.y, f->spawn.y, kTol);
    }
}

TEST(SimMuzzleTest, MuzzleIsRelativeToThePlayerAndTheBulletStillFliesAlongTheAim) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const glm::vec2 pos{123.0F, -45.0F};
    const First f = FireOnce(gd, {}, {-1.0F, 0.0F}, pos);
    EXPECT_NEAR(f.spawn.x, pos.x - kGx, kTol);
    EXPECT_NEAR(f.spawn.y, pos.y + kGy, kTol);
    // Only the ORIGIN moved: the heading is the aim (deviation 0), speed unchanged.
    const float speed = 36.0F * Game::Scale::kCellPx;
    EXPECT_NEAR(f.vel.x, -speed, 1e-2F);
    EXPECT_NEAR(f.vel.y, 0.0F, 1e-2F);
}

TEST(SimMuzzleTest, WithoutHeldDataTheOriginIsThePlayerAsBefore) {
    // A hand-built weapon (no `held`) keeps the pre-slice origin: every legacy golden / replay stays byte-identical.
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    Game::WeaponDef w = *gd.FindWeapon("weapon_000");
    w.held = Game::HeldWeaponDef{};
    w.deviation = 0;
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    sim.EquipWeapon(w, "Gun001", 5, &gd);
    sim.SetPlayerHand({0.0F, 16.0F}, {0.0F, 16.0F});
    sim.Advance(20.0F, Fire({1.0F, 0.0F}, {10.0F, 20.0F}));
    ASSERT_EQ(sim.Bullets().size(), 1U);
    EXPECT_EQ(sim.Bullets()[0].prevPos, glm::vec2(10.0F, 20.0F));
    EXPECT_EQ(sim.PlayerMuzzle({10.0F, 20.0F}, {1.0F, 0.0F}, 1), glm::vec2(10.0F, 20.0F));
    // A held sprite without a gun_point also keeps the body origin.
    w.held.present = true;
    w.held.hasGunPoint = false;
    sim.EquipWeapon(w, "Gun001", 5, &gd);
    EXPECT_EQ(sim.PlayerMuzzle({10.0F, 20.0F}, {1.0F, 0.0F}, 1), glm::vec2(10.0F, 20.0F));
    // A non-finite gun_point (corrupt data) is rejected rather than poisoning the bullet position.
    w.held.hasGunPoint = true;
    w.held.gunPointX = std::numeric_limits<float>::quiet_NaN();
    sim.EquipWeapon(w, "Gun001", 5, &gd);
    EXPECT_EQ(sim.PlayerMuzzle({10.0F, 20.0F}, {1.0F, 0.0F}, 1), glm::vec2(10.0F, 20.0F));
}

TEST(SimMuzzleTest, HandMountOffsetMirrorsWithTheFacing) {
    // c08-like hand h1 (0.4, 0.6) units = (12.8, 19.2) px, body centre 16 px above the feet: the hand is
    // 3.2 px above the body and 12.8 px to the side of it, on the side the hero faces.
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    Game::WeaponDef w = *gd.FindWeapon("weapon_000");
    w.deviation = 0;
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    sim.EquipWeapon(w, "Gun001", 5, &gd);
    sim.SetPlayerHand({12.8F, 19.2F}, {0.0F, 16.0F});
    // facing +1, aim +x: hand (12.8, 3.2) + gun_point (19.2, 4.8)
    const glm::vec2 right = sim.PlayerMuzzle({0.0F, 0.0F}, {1.0F, 0.0F}, 1);
    EXPECT_NEAR(right.x, 12.8F + kGx, kTol);
    EXPECT_NEAR(right.y, 3.2F + kGy, kTol);
    // facing -1, aim -x: hand (-12.8, 3.2) + (-19.2, +4.8)
    const glm::vec2 left = sim.PlayerMuzzle({0.0F, 0.0F}, {-1.0F, 0.0F}, -1);
    EXPECT_NEAR(left.x, -12.8F - kGx, kTol);
    EXPECT_NEAR(left.y, 3.2F + kGy, kTol);
}

TEST(SimMuzzleTest, FacingTracksTheAimSideAndSurvivesBadAim) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    Game::Sim::Simulation sim(20240607, &g_NullWorld);
    EXPECT_EQ(sim.PlayerFacing(), 1);
    auto step = [&](glm::vec2 aim) {
        Game::Sim::WorldInputs in = Fire(aim);
        in.firing = false;
        sim.Advance(20.0F, in);
        return sim.PlayerFacing();
    };
    EXPECT_EQ(step({-1.0F, 0.2F}), -1);
    EXPECT_EQ(step({0.0F, -1.0F}), -1); // vertical: keeps left
    EXPECT_EQ(step({std::numeric_limits<float>::quiet_NaN(), 1.0F}), -1); // NaN aim: keeps left
    EXPECT_EQ(step({0.7F, 0.7F}), 1);
    EXPECT_EQ(step({0.0F, 1.0F}), 1);
}

TEST(SimMuzzleTest, MovingTheOriginDrawsNoExtraRandomNumbers) {
    // The scatter cone (deviation 5) is rolled once per shot from the weapon's own stream. Re-seating the origin at
    // the muzzle must leave that stream untouched: the bullet HEADINGS are bit-identical with and without `held`.
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    auto headings = [&](bool held) {
        Game::WeaponDef w = *gd.FindWeapon("weapon_000");
        EXPECT_GT(w.deviation, 0);
        if (!held) {
            w.held = Game::HeldWeaponDef{};
        }
        Game::Sim::Simulation sim(20240607, &g_NullWorld);
        sim.EquipWeapon(w, "Gun001", 5, &gd);
        std::vector<glm::vec2> out;
        for (int i = 0; i < 80; ++i) {
            sim.Advance(20.0F, Fire({1.0F, 0.0F}));
        }
        for (const Game::Sim::BulletState &b : sim.Bullets()) {
            out.push_back(glm::normalize(b.vel));
        }
        return out;
    };
    const std::vector<glm::vec2> a = headings(true);
    const std::vector<glm::vec2> b = headings(false);
    ASSERT_FALSE(a.empty());
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i], b[i]) << "bullet " << i;
    }
}

} // namespace
