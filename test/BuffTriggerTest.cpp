// P3.2: bullets that carry a *BuffTrigger component apply the matching buff on
// hit. Covers (a) the data shape (tools/build_gamedata.py's attach_bullet_buffs()
// -> GameData::BulletDef::buffId/buffProbability) and (b) the sim wiring
// (BrainFactory::MakeWeapon -> WeaponController::Params -> FireIntent ->
// BulletState -> Simulation::RollBulletBuff/ApplyBuff).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "SweptOracle.hpp"
#include "combat/Buff.hpp"
#include "data/GameData.hpp"
#include "data/RGRandom.hpp"
#include "sim/BodyGeom.hpp"
#include "sim/SimConfig.hpp"
#include "sim/SimEvent.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"

using Game::Sim::Simulation;
using Game::Sim::SimEvent;
using Game::Sim::SimEventType;
using Game::Sim::WorldInputs;

// NOLINTBEGIN(readability-magic-numbers)

namespace {
Game::Sim::NullWorldCollision g_NullWorld;
const char *kResourceRoot = RESOURCE_DIR; // CMake-injected, as in GameDataTest.

WorldInputs FiringAt(glm::vec2 aim) {
    WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = aim;
    in.firing = true;
    in.playerRoomId = 0;
    return in;
}

/// True iff any drained event this call is a "buff_applied:<kind>:..." AnimTrigger.
bool HasBuffApplied(const std::vector<SimEvent> &events, const char *kindSubstr) {
    const std::string needle = std::string("buff_applied:") + kindSubstr;
    for (const SimEvent &e : events) {
        if (e.type == SimEventType::AnimTrigger && e.name.rfind(needle, 0) == 0) {
            return true;
        }
    }
    return false;
}
} // namespace

// --- (a) data: bullets.json "buff" -> GameData::BulletDef::buffId/buffProbability ---

TEST(BuffTriggerTest, DataBulletFireBulletTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_4"); // RGBulletBuffTrigger -> buff_fire
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffFire");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataArrowFireBulletTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_36"); // RGArrowBuffTrigger -> buff_fire
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffFire");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataSwordFireBuffOnlyRow) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    // sword_2_fire doesn't derive from the RGBullet family -- attach_bullet_buffs()
    // appends it as a NEW buff-only bullets.json row (no speed/destroy_time).
    const Game::BulletDef *b = gd.FindBullet("sword_2_fire"); // RGSwordBuffTrigger -> buff_fire
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffFire");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataBulletProbabilityZeroIsCritOnly) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_95"); // BuffEffectTrigger, probability: 0
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffIce");
    EXPECT_FLOAT_EQ(b->buffProbability, 0.0F);
}

TEST(BuffTriggerTest, DataPoisonBulletTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_3"); // RGBulletBuffTrigger -> buff_posion
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffPoison");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataElectricBulletTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_32"); // RGBulletBuffTrigger -> buff_ele
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffElectric");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataIceArrowTrigger) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_38"); // RGArrowBuffTrigger -> buff_ice
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->buffId, "BuffIce");
    EXPECT_FLOAT_EQ(b->buffProbability, 1.0F);
}

TEST(BuffTriggerTest, DataNoBuffBulletStaysEmpty) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::BulletDef *b = gd.FindBullet("bullet_0"); // ReboundBuffEffector only -- NOT a buff trigger.
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->buffId.empty());
    EXPECT_FLOAT_EQ(b->buffProbability, 0.0F);
}

TEST(BuffTriggerTest, DataWeaponResolvesItsBullet) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::WeaponDef *w = gd.FindWeapon("weapon_013"); // Gun001, bullet_4 (fire)
    ASSERT_NE(w, nullptr);
    const Game::BulletDef *b = gd.BulletForWeapon(*w);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->id, "bullet_4");
    EXPECT_EQ(b->buffId, "BuffFire");
}

// --- (b) sim: bullet-trigger wiring auto-applies the buff on a landed hit ---

TEST(BuffTriggerTest, FireBulletWeaponAppliesFireOnHit) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemyByClass("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("weapon_013"); // Gun001 class, bullet_4 (fire, p=1.0)
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    Simulation sim(20240607, &g_NullWorld);
    sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, 21000);
    sim.EquipWeapon(*wdef, "weapon_013", 5, &gd); // P3.2: data threads the bullet's buff in.

    const WorldInputs in = FiringAt(glm::vec2{1.0F, 0.0F});
    bool sawFireApplied = false;
    int hpRightAfterApply = -1;
    for (int i = 0; i < 150 && !sawFireApplied; ++i) {
        sim.Advance(20.0F, in);
        if (HasBuffApplied(sim.DrainEvents(), "Fire")) {
            sawFireApplied = true;
            hpRightAfterApply = sim.EnemyViews()[0].hp;
        }
    }
    ASSERT_TRUE(sawFireApplied) << "bullet_4's RGBulletBuffTrigger (probability 1.0) never applied Fire";

    // The Fire buff's DoT (1 dmg/s, 2s duration -- buffs.json BuffFire) ticks
    // through TickBuffs() even with no further hits; hp must keep dropping.
    bool sawDot = false;
    for (int i = 0; i < 100; ++i) {
        WorldInputs idle = in;
        idle.firing = false; // stop shooting -- isolate the DoT from further direct hits.
        sim.Advance(20.0F, idle);
        if (!sim.EnemyViews()[0].alive) {
            break; // enemy already low-hp from the direct hit; DoT may finish it off.
        }
        if (sim.EnemyViews()[0].hp < hpRightAfterApply) {
            sawDot = true;
            break;
        }
    }
    EXPECT_TRUE(sawDot) << "Fire's tick damage never reduced the enemy's hp after the initial hit";
}

// --- the zero/negative-probability branch of BuffEffectTrigger$$TriggerWith
// (game_typed.c:86608-86645). NOT "never applies": the decompile skips ONLY when
// the hit is not a crit AND the bullet's camp == 0. Crit is forced deterministically
// by overriding the WeaponDef's `critical` (0..100, the crit-roll threshold in
// Combat::ResolveHit: crit iff Range(0,100) < critical -- so 0 never crits and
// 100 always crits, with no reliance on a particular RNG seed). ---

TEST(BuffTriggerTest, ProbabilityZeroNonCritPlayerBulletNeverAppliesBuff) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemyByClass("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("weapon_175"); // Gun001, bullet_95 (ice, p=0.0)
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);
    Game::WeaponDef noCrit = *wdef;
    noCrit.critical = 0; // Range(0,100) < 0 is never true -> every hit is non-crit.

    Simulation sim(1, &g_NullWorld);
    sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, 21000);
    sim.EquipWeapon(noCrit, "weapon_175", 5, &gd);

    const WorldInputs in = FiringAt(glm::vec2{1.0F, 0.0F});
    for (int i = 0; i < 150; ++i) {
        sim.Advance(20.0F, in);
        EXPECT_FALSE(HasBuffApplied(sim.DrainEvents(), "Ice"))
            << "bullet_95 has buff.probability 0 and this is a NON-crit player "
               "(camp 0) bullet -- TriggerWith returns before AddBuff";
        if (!sim.EnemyViews()[0].alive) {
            break;
        }
    }
}

TEST(BuffTriggerTest, ProbabilityZeroCritBulletDoesApplyBuff) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemyByClass("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("weapon_175"); // bullet_95, probability 0.0
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);
    Game::WeaponDef allCrit = *wdef;
    allCrit.critical = 100; // Range(0,100) is 0..99 -> always < 100 -> every hit crits.

    Simulation sim(1, &g_NullWorld);
    sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, 21000);
    sim.EquipWeapon(allCrit, "weapon_175", 5, &gd);

    const WorldInputs in = FiringAt(glm::vec2{1.0F, 0.0F});
    bool sawIce = false;
    for (int i = 0; i < 150 && !sawIce; ++i) {
        sim.Advance(20.0F, in);
        sawIce = HasBuffApplied(sim.DrainEvents(), "Ice");
        if (!sim.EnemyViews()[0].alive) {
            break;
        }
    }
    EXPECT_TRUE(sawIce) << "probability 0 + isCritic must APPLY -- TriggerWith only "
                           "returns early when !isCritic AND camp == 0";
}

// --- the decision rule itself, all four branches, no sim needed --------------
// Simulation::ShouldApplyBulletBuff(probability[0..100], isCritic, camp, rng).

TEST(BuffTriggerTest, ShouldApplyBulletBuffZeroProbabilityCritCampMatrix) {
    Game::RGRandom rng;
    rng.FallbackSeed = 1234;
    // probability <= 0 draws NO rng at all (faithful: TriggerWith calls
    // Random.Range only on the else branch), so the stream stays untouched --
    // asserted below by the stream still being unseeded after 12 decisions.
    for (float p : {0.0F, -1.0F, -100.0F}) {
        EXPECT_FALSE(Simulation::ShouldApplyBulletBuff(p, /*isCritic=*/false, /*camp=*/0, rng))
            << "p=" << p << ": not a crit AND camp 0 is the ONLY skip";
        EXPECT_TRUE(Simulation::ShouldApplyBulletBuff(p, /*isCritic=*/true, /*camp=*/0, rng))
            << "p=" << p << ": crit applies regardless of camp";
        EXPECT_TRUE(Simulation::ShouldApplyBulletBuff(p, /*isCritic=*/false, /*camp=*/1, rng))
            << "p=" << p << ": camp != 0 applies even without a crit";
        EXPECT_TRUE(Simulation::ShouldApplyBulletBuff(p, /*isCritic=*/true, /*camp=*/1, rng));
    }
    // RGRandom lazy-seeds on its FIRST draw, so an unseeded stream after all 12
    // decisions above is positive proof the probability <= 0 branch never rolled.
    EXPECT_FALSE(rng.Seeded());
}

// Boundary of the roll branch: apply iff `probability > roll`, with
// roll = Range(0, 100) (max EXCLUSIVE -> 0..99). Replaying the SAME seeded
// stream gives the same roll sequence, so each roll can be probed from both
// sides of the comparison.
TEST(BuffTriggerTest, ShouldApplyBulletBuffRollBoundaryIsStrictGreaterThan) {
    auto rolls = [] {
        Game::RGRandom r;
        r.FallbackSeed = 4242;
        std::vector<int> out;
        out.reserve(16);
        for (int i = 0; i < 16; ++i) {
            out.push_back(r.Range(0, 100));
        }
        return out;
    }();
    ASSERT_EQ(rolls.size(), 16U);
    for (int r : rolls) {
        ASSERT_GE(r, 0);
        ASSERT_LE(r, 99); // Range(int,int) is max-exclusive.
    }

    auto decide = [](float percent, std::size_t draws) {
        Game::RGRandom r;
        r.FallbackSeed = 4242;
        bool last = false;
        for (std::size_t i = 0; i < draws; ++i) {
            last = Simulation::ShouldApplyBulletBuff(percent, /*isCritic=*/false,
                                                     /*camp=*/0, r);
        }
        return last;
    };
    for (std::size_t i = 0; i < rolls.size(); ++i) {
        const float roll = static_cast<float>(rolls[i]);
        // probability == roll -> `probability <= roll` -> SKIP (the decompile's
        // early return); probability == roll + 1 -> apply.
        EXPECT_FALSE(decide(roll, i + 1)) << "roll " << rolls[i] << ": equal must skip";
        EXPECT_TRUE(decide(roll + 1.0F, i + 1)) << "roll " << rolls[i] << ": greater must apply";
    }
}

TEST(BuffTriggerTest, ShouldApplyBulletBuffHundredPercentAlwaysApplies) {
    Game::RGRandom rng;
    rng.FallbackSeed = 99;
    // Range(0,100) can never return 100, so 100 > roll always holds. This is the
    // branch every INFERRED (T-33) probability-1.0 row takes.
    for (int i = 0; i < 200; ++i) {
        EXPECT_TRUE(Simulation::ShouldApplyBulletBuff(100.0F, false, 0, rng));
    }
    // ... and a 1-percent chance must NOT always apply over 200 draws.
    Game::RGRandom rng2;
    rng2.FallbackSeed = 99;
    int applied = 0;
    for (int i = 0; i < 200; ++i) {
        applied += Simulation::ShouldApplyBulletBuff(1.0F, false, 0, rng2) ? 1 : 0;
    }
    EXPECT_LT(applied, 200);
}

TEST(BuffTriggerTest, ProbabilityRollIsDeterministicUnderFixedSeed) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemyByClass("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("weapon_046"); // Gun003, bullet_9 (RGLaserBuff, p=0.04)
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    auto run = [&](int runSeed) {
        Simulation sim(runSeed, &g_NullWorld);
        sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, runSeed + 1000);
        sim.EquipWeapon(*wdef, "weapon_046", runSeed + 5, &gd);
        const WorldInputs in = FiringAt(glm::vec2{1.0F, 0.0F});
        std::vector<float> trace;
        for (int i = 0; i < 200; ++i) {
            sim.Advance(20.0F, in);
            const auto events = sim.DrainEvents();
            trace.push_back(HasBuffApplied(events, "Ice") ? 1.0F : 0.0F);
            trace.push_back(sim.EnemyViews().empty() ? -1.0F
                                                     : static_cast<float>(sim.EnemyViews()[0].hp));
        }
        return trace;
    };
    const std::vector<float> a = run(777);
    const std::vector<float> b = run(777);
    EXPECT_EQ(a, b); // same seed -> the probability roll (m_HitRng) replays identically.
}

// --- existing golden behaviour: a weapon whose bullet carries NO buff (Gun001's
// default weapon_000/bullet_0) stays exactly as it was before P3.2. ---
TEST(BuffTriggerTest, NoBuffWeaponUnaffectedByP32Wiring) {
    Game::GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::EnemyDef *edef = gd.FindEnemyByClass("EnemyAI01");
    const Game::WeaponDef *wdef = gd.FindWeapon("Gun001"); // class fallback -> weapon_000/bullet_0
    ASSERT_NE(edef, nullptr);
    ASSERT_NE(wdef, nullptr);

    // T-57 (swept collision): the tick of each hit is the first fixed step whose bullet path
    // (start -> end of step, relative to the walking enemy) touches the hurtbox grown by the
    // bullet radius -- a continuous quantity, so `run` also re-derives that tick independently
    // from the recorded positions with the INDEPENDENT brute-force oracle of SweptOracle.hpp
    // (2000 sub-steps of the first bullet's straight flight and the enemy's per-step motion,
    // a separately written discrete overlap test; no production collision code) and the sim
    // must agree with it EXACTLY.
    const Game::BulletDef *bd = gd.BulletForWeapon(*wdef);
    ASSERT_NE(bd, nullptr);
    const Game::Sim::BodyGeom geom = Game::Sim::MakeBodyGeom(edef->body, edef->hurtbox, 16.0F);
    struct Run {
        std::vector<float> trace;
        int oracleTick = -1;
    };
    auto run = [&](bool passData) {
        Simulation sim(20240607, &g_NullWorld);
        sim.AddEnemy(*edef, glm::vec2{60.0F, 0.0F}, /*roomId=*/0, 21000);
        sim.EquipWeapon(*wdef, "Gun001", 5, passData ? &gd : nullptr);
        const float bulletR =
            passData ? Game::Sim::EquivalentRadiusPx(bd->collider) : Game::Sim::kBulletRadius;
        const WorldInputs in = FiringAt(glm::vec2{1.0F, 0.0F});
        Run r;
        r.trace.push_back(static_cast<float>(sim.EnemyViews()[0].hp)); // [0] = hp before any tick
        glm::vec2 prevEnemy = sim.EnemyViews()[0].pos;
        glm::vec2 bulletStep{0.0F, 0.0F}; // first bullet's displacement per step (straight flight)
        glm::vec2 bulletEnd{0.0F, 0.0F};
        for (int i = 0; i < 150; ++i) {
            sim.Advance(20.0F, in);
            r.trace.push_back(static_cast<float>(sim.EnemyViews()[0].hp));
            EXPECT_FALSE(HasBuffApplied(sim.DrainEvents(), "")); // no buff_applied event AT ALL.
            const glm::vec2 enemy = sim.EnemyViews()[0].pos;
            if (i == 0) {
                EXPECT_EQ(sim.Bullets().size(), 1U);
                bulletStep = sim.Bullets()[0].vel * Game::Sim::kFixedStepSeconds;
                bulletEnd = sim.Bullets()[0].pos; // end of step 1 (it spawned at the muzzle and moved once)
            } else {
                bulletEnd += bulletStep;
            }
            if (r.oracleTick < 0 &&
                sktest::oracle::FirstTouchSub(geom, prevEnemy, enemy, bulletEnd - bulletStep,
                                              bulletEnd, bulletR) >= 0) {
                r.oracleTick = i + 1; // trace index of this step
            }
            prevEnemy = enemy;
        }
        return r;
    };
    // Passing GameData in (P3.2) must not change a no-buff weapon's DAMAGE SEQUENCE
    // versus the old (nullptr) call sites' behaviour. COLLIDER DATA (scale pass): with
    // data the weapon's bullet takes its prefab collider radius (bullet_0: 0.5x0.4-unit box
    // -> 6.4 px) instead of the 4 px fallback, so it touches the enemy up to one tick EARLIER.
    // Compare the sequence of distinct hp values (same hits, same damage) first.
    auto distinct = [](std::vector<float> v) {
        v.erase(std::unique(v.begin(), v.end()), v.end());
        return v;
    };
    const Run withData = run(/*passData=*/true);
    const Run fallback = run(/*passData=*/false);
    EXPECT_EQ(distinct(withData.trace), distinct(fallback.trace));
    auto firstHit = [](const std::vector<float> &t) {
        for (std::size_t i = 1; i < t.size(); ++i) {
            if (t[i] < t[0]) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    // PRECISE (T-57): each run's first-damage tick IS the independently derived swept-contact
    // tick. (P3.0 had loosened this to "no later, at most one earlier" because the discrete
    // end-of-step test aliased with the 23 px step; the swept hit time is continuous, so the
    // radius change moves the tick only when a tick boundary lies inside the 2.4 px of extra
    // reach, and the oracle pins which of the two outcomes the data gives.)
    ASSERT_GT(firstHit(fallback.trace), 1);
    RecordProperty("firstHitData", firstHit(withData.trace));
    RecordProperty("firstHitFallback", firstHit(fallback.trace));
    EXPECT_EQ(firstHit(withData.trace), withData.oracleTick);
    EXPECT_EQ(firstHit(fallback.trace), fallback.oracleTick);
    // ...and the larger data radius can never be LATER, nor more than one tick (23 px of
    // travel > 2.4 px of reach) EARLIER.
    EXPECT_LE(firstHit(withData.trace), firstHit(fallback.trace));
    EXPECT_GE(firstHit(withData.trace), firstHit(fallback.trace) - 1);
}

// NOLINTEND(readability-magic-numbers)
