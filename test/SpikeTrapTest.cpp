#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldInputs.hpp"
#include "world/SpikeTrap.hpp"

using Game::SpikeTrap;
using Game::StingDef;

// NOLINTBEGIN(readability-magic-numbers)

// P2.20 AC4: the sting01 trap. FAITHFUL @ ObjectSting (DISASM 0x487aac-0x488520) + the prefab numbers
// (damage 2, offet_time 0, show_time 1, hide_time 2, hit_player / hit_enemy true): up 1 s / down 2 s, a damage
// tick every 0.3 s while up (4 per cycle), none while down, enemies take damage / 2.
namespace {

const char *kResourceRoot = RESOURCE_DIR;

StingDef PrefabSting() {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const Game::LevelDef *a1 = gd.FindLevel("A1");
    EXPECT_NE(a1, nullptr);
    return a1->obstacles.at(5).sting; // sting01
}

struct Step {
    double now = 0.0;
    bool up = false;
    bool hit = false;
};

// Drive a started trap with @p stepMs steps for @p totalMs, recording every step.
std::vector<Step> Drive(SpikeTrap &trap, double stepMs, double totalMs) {
    std::vector<Step> out;
    for (double t = 0.0; t < totalMs; t += stepMs) {
        const bool hit = trap.Advance(stepMs);
        out.push_back(Step{trap.NowMs(), trap.Up(), hit});
    }
    return out;
}

} // namespace

TEST(SpikeTrapTest, PrefabNumbersAreTheOriginalsAndOnlyStingPrefabsCarryThem) {
    Game::GameData gd;
    gd.LoadAll(kResourceRoot);
    const StingDef d = PrefabSting();
    ASSERT_TRUE(d.present);
    EXPECT_EQ(d.damage, 2);
    EXPECT_FLOAT_EQ(d.offsetTime, 0.0F);
    EXPECT_FLOAT_EQ(d.showTime, 1.0F);
    EXPECT_FLOAT_EQ(d.hideTime, 2.0F);
    EXPECT_TRUE(d.hitPlayer);
    EXPECT_TRUE(d.hitEnemy);
    EXPECT_EQ(d.base.sprite, "objects1_0.png");
    EXPECT_EQ(d.ding.sprite, "objects1_1.png");
    EXPECT_FLOAT_EQ(d.base.color[0], 0.890196F);
    // across all 31 levels: a trap exists exactly where the obstacle prefab is sting01
    int stings = 0;
    for (const Game::LevelDef &lv : gd.Levels()) {
        for (const Game::LevelObstacleDef &o : lv.obstacles) {
            EXPECT_EQ(o.sting.present, o.prefab == "sting01") << lv.id << " " << o.prefab;
            stings += o.sting.present ? 1 : 0;
        }
    }
    EXPECT_GT(stings, 20);
    EXPECT_EQ(gd.RoomProps().stings.count("sting01"), 1u);
}

// AC4: the exact up / down timeline at the game's 20 ms step: up [20,1020), down [1020,3020), up [3020,4020);
// damage ticks at +0 / +0.3 / +0.6 / +0.9 s of each up phase and nowhere else.
TEST(SpikeTrapTest, TogglesOneSecondUpTwoSecondsDownWithAFourTickCycle) {
    SpikeTrap trap(PrefabSting());
    // before OnGameStateChange(1): never up, never hits
    for (const Step &s : Drive(trap, 20.0, 5000.0)) {
        EXPECT_FALSE(s.up);
        EXPECT_FALSE(s.hit);
    }
    SpikeTrap t2(PrefabSting());
    EXPECT_FALSE(t2.Started());
    t2.OnRoomStarted();
    EXPECT_TRUE(t2.Started());
    const std::vector<Step> run = Drive(t2, 20.0, 7000.0);
    std::vector<double> hits;
    std::vector<double> ups, downs;
    bool prev = false;
    for (const Step &s : run) {
        if (s.hit) {
            hits.push_back(s.now);
            EXPECT_TRUE(s.up) << "a damage tick never lands while the spike is down (t=" << s.now << ")";
        }
        if (s.up && !prev) {
            ups.push_back(s.now);
        }
        if (!s.up && prev) {
            downs.push_back(s.now);
        }
        prev = s.up;
    }
    // ShowSting fires on the first advance (offet_time 0), HideSting 1.0 s later, ShowSting again hide_time
    // (2.0 s) after that: a 3 s period
    EXPECT_EQ(ups, (std::vector<double>{20.0, 3020.0, 6020.0}));
    EXPECT_EQ(downs, (std::vector<double>{1020.0, 4020.0}));
    const std::vector<double> expected{20.0, 320.0, 620.0, 920.0, 3020.0, 3320.0, 3620.0, 3920.0,
                                       6020.0, 6320.0, 6620.0, 6920.0};
    EXPECT_EQ(hits, expected);
}

// The 0.3 s tick is one decision, not a cadence of the step: the same timeline at 10 ms and 50 ms steps.
TEST(SpikeTrapTest, TimelineDoesNotDependOnTheStepSizeBeyondItsGranularity) {
    for (const double step : {10.0, 50.0}) {
        SpikeTrap t(PrefabSting());
        t.OnRoomStarted();
        int ticksInFirstCycle = 0;
        bool downSeen = false;
        for (const Step &s : Drive(t, step, 3000.0)) {
            if (s.hit && s.now <= 1500.0) {
                ++ticksInFirstCycle;
            }
            downSeen |= !s.up;
            if (s.hit) {
                EXPECT_TRUE(s.up);
            }
        }
        EXPECT_EQ(ticksInFirstCycle, 4) << "step " << step;
        EXPECT_TRUE(downSeen) << "step " << step;
    }
}

// offet_time: the first ShowSting waits for it.
TEST(SpikeTrapTest, OffsetTimeDelaysTheFirstShow) {
    StingDef d = PrefabSting();
    d.offsetTime = 0.5F;
    SpikeTrap t(d);
    t.OnRoomStarted();
    double firstUp = -1.0;
    for (const Step &s : Drive(t, 20.0, 2000.0)) {
        if (s.up && firstUp < 0) {
            firstUp = s.now;
        }
    }
    EXPECT_DOUBLE_EQ(firstUp, 500.0);
}

// CancelInvoke at HideSting: a TurnCanHit pending at the moment the spike drops must not fire later. With
// two huge frames the first one starts the cycle, the second runs TurnCanHit + HideSting in one pass; no hit
// is reported although can_hit became true, because show_sting is false by the time Update runs.
TEST(SpikeTrapTest, HideCancelsThePendingTickAndAHitNeedsTheSpikeUp) {
    SpikeTrap t(PrefabSting());
    t.OnRoomStarted();
    EXPECT_TRUE(t.Advance(5000.0)); // ShowSting + Update: tick 1
    EXPECT_TRUE(t.Up());
    EXPECT_FALSE(t.Advance(5000.0)); // TurnCanHit (due +300) then HideSting (due +1000) in one pass
    EXPECT_FALSE(t.Up());
    EXPECT_EQ(t.PendingTimers(), 1u) << "only the next ShowSting survives CancelInvoke";
    EXPECT_FALSE(t.Advance(10.0));
    EXPECT_FALSE(t.Up());
}

TEST(SpikeTrapTest, EnemyDamageIsHalfTheDamageTruncatedTowardZero) {
    for (const auto &[dmg, half] : std::vector<std::pair<int, int>>{{2, 1}, {3, 1}, {1, 0}, {5, 2}, {0, 0}}) {
        StingDef d = PrefabSting();
        d.damage = dmg;
        EXPECT_EQ(SpikeTrap(d).EnemyDamage(), half) << dmg;
    }
}

// ---- damage gating through the simulation (what the scene does on a hit tick) --------------------------

namespace {
Game::Sim::WorldInputs InputAt(glm::vec2 p) {
    Game::Sim::WorldInputs in;
    in.playerPos = p;
    in.playerRoomId = 5; // a different room than the enemy's: it stays asleep
    in.aimDir = glm::vec2{1.0F, 0.0F};
    return in;
}

Game::CombatStats Vitals(int hp, int armor) {
    Game::CombatStats s;
    s.hp = hp;
    s.maxHp = hp;
    s.armor = armor;
    s.maxArmor = armor;
    return s;
}
} // namespace

// AC4 gating, R48: the four ticks of an up phase are four hit CHECKS, not four hits. An effective hit starts the hero's 0.5 s
// hit protection (RGController.StartHitTrigger(0.5f), kPlayerHitProtectMs), so a hero standing on one trap cell for the
// whole 7 s timeline is hurt by ticks 1 and 3 of every up phase only (t = 20, 620, 3020, 3620, 6020, 6620 ms): the 12 checks
// cost 6 hits x 2 = 12 vitals; the swallowed checks (320, 920, ...) report `playerProtected`; nothing lands while down.
TEST(SpikeTrapTest, StandingOnOneCellTwelveChecksSixEffectiveHits) {
    Game::Sim::Simulation sim(1, nullptr);
    sim.SetPlayerStats(Vitals(30, 0));
    SpikeTrap trap(PrefabSting());
    trap.OnRoomStarted();
    const glm::vec2 cell{100.0F, 100.0F};
    sim.Advance(20.0F, InputAt(cell)); // publish the player's position to the sim
    std::vector<double> checks, hits, swallowed;
    int hpAfterFirstCycle = -1;
    int hpAtDownEnd = -1;
    for (double t = 0.0; t < 7000.0; t += 20.0) {
        sim.Advance(20.0F, InputAt(cell)); // one fixed step: the protection timer runs in the sim
        const int before = sim.PlayerStats().hp;
        const bool check = trap.Advance(20.0);
        if (check) {
            checks.push_back(trap.NowMs());
            const auto r = sim.ApplyHazardBox(cell, glm::vec2{16.0F, 16.0F}, trap.Damage(), trap.EnemyDamage(),
                                              trap.HitPlayer(), trap.HitEnemy(), false);
            EXPECT_NE(r.player, r.playerProtected) << "overlapping: exactly one of effective / protected";
            if (r.player) {
                hits.push_back(trap.NowMs());
                EXPECT_EQ(sim.PlayerStats().hp, before - 2);
                EXPECT_TRUE(sim.PlayerHitProtected());
            } else {
                swallowed.push_back(trap.NowMs());
                EXPECT_EQ(sim.PlayerStats().hp, before) << "a protected check deals nothing";
            }
        } else {
            EXPECT_EQ(sim.PlayerStats().hp, before) << "no damage on a non-tick step (t=" << trap.NowMs() << ")";
        }
        if (trap.NowMs() == 1020.0) {
            hpAfterFirstCycle = sim.PlayerStats().hp;
        }
        if (trap.NowMs() == 3000.0) {
            hpAtDownEnd = sim.PlayerStats().hp;
        }
    }
    EXPECT_EQ(checks.size(), 12u);
    EXPECT_EQ(hits, (std::vector<double>{20, 620, 3020, 3620, 6020, 6620}));
    EXPECT_EQ(swallowed, (std::vector<double>{320, 920, 3320, 3920, 6320, 6920}));
    EXPECT_EQ(hpAfterFirstCycle, 30 - 4) << "2 effective hits in the first up phase, not 4";
    EXPECT_EQ(hpAtDownEnd, 30 - 4) << "the whole 2 s down phase cost nothing";
    EXPECT_EQ(sim.PlayerStats().hp, 30 - 12);
}

// R48: two trap cells under the hero on the same tick cost ONE hit (the first starts the protection, the second finds
// the hurtbox off); the old per-cell damage dealt it twice.
TEST(SpikeTrapTest, AdjacentCellsOnTheSameTickCostOneHit) {
    Game::Sim::Simulation sim(1, nullptr);
    sim.SetPlayerStats(Vitals(30, 0));
    sim.Advance(20.0F, InputAt({0.0F, 0.0F}));
    const glm::vec2 half{16.0F, 16.0F};
    const auto a = sim.ApplyHazardBox({-16.0F, 0.0F}, half, 2, 1, true, true, false);
    const auto b = sim.ApplyHazardBox({16.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_TRUE(a.player);
    EXPECT_FALSE(a.playerProtected);
    EXPECT_FALSE(b.player);
    EXPECT_TRUE(b.playerProtected);
    EXPECT_EQ(sim.PlayerStats().hp, 28);
    // the pre-R48 behaviour (test switch): both land
    Game::Sim::Simulation old(1, nullptr);
    old.SetHitProtectionEnabled(false);
    old.SetPlayerStats(Vitals(30, 0));
    old.Advance(20.0F, InputAt({0.0F, 0.0F}));
    old.ApplyHazardBox({-16.0F, 0.0F}, half, 2, 1, true, true, false);
    old.ApplyHazardBox({16.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_EQ(old.PlayerStats().hp, 26);
}

// R48: the protection lasts exactly 0.5 s: 24 fixed steps later the hero is still protected, after the 25th the check lands.
TEST(SpikeTrapTest, HitProtectionExpiresAfterHalfASecond) {
    Game::Sim::Simulation sim(1, nullptr);
    sim.SetPlayerStats(Vitals(30, 0));
    sim.Advance(20.0F, InputAt({0.0F, 0.0F}));
    const glm::vec2 half{16.0F, 16.0F};
    EXPECT_FALSE(sim.PlayerHitProtected());
    EXPECT_TRUE(sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false).player);
    EXPECT_FLOAT_EQ(sim.PlayerHitProtectMs(), 500.0F);
    for (int i = 0; i < 24; ++i) {
        sim.Advance(20.0F, InputAt({0.0F, 0.0F}));
    }
    EXPECT_TRUE(sim.PlayerHitProtected());
    EXPECT_TRUE(sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false).playerProtected);
    EXPECT_EQ(sim.PlayerStats().hp, 28);
    sim.Advance(20.0F, InputAt({0.0F, 0.0F})); // the 25th step: EndHitTrigger
    EXPECT_FALSE(sim.PlayerHitProtected());
    EXPECT_TRUE(sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false).player);
    EXPECT_EQ(sim.PlayerStats().hp, 26);
}

// R48/R50: a miss starts no protection; a ZERO-damage hit that reaches GetHurt is the NoDamage outcome (reachable, and the only way
// to it): nothing lost, neither `player` nor `playerProtected`, and -- INFERRED (T-77, HurtArmor / HurtHp call StartHitTrigger
// without asking for a loss) -- it still starts the protection, so the next check is Protected.
TEST(SpikeTrapTest, MissStartsNothingAndZeroDamageIsTheNoDamageOutcome) {
    Game::Sim::Simulation sim(1, nullptr);
    sim.SetPlayerStats(Vitals(30, 0));
    sim.Advance(20.0F, InputAt({0.0F, 0.0F}));
    const glm::vec2 half{16.0F, 16.0F};
    const auto miss = sim.ApplyHazardBox({400.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_FALSE(miss.player || miss.playerProtected || miss.playerAbsorbed || miss.playerNoDamage);
    EXPECT_FALSE(sim.PlayerHitProtected());
    const auto zero = sim.ApplyHazardBox({0.0F, 0.0F}, half, 0, 0, true, false, false); // hit_player only, damage 0
    EXPECT_TRUE(zero.playerNoDamage);
    EXPECT_FALSE(zero.player) << "nothing was lost: not an effective hit";
    EXPECT_FALSE(zero.playerProtected);
    EXPECT_FALSE(zero.playerAbsorbed);
    EXPECT_EQ(sim.PlayerStats().hp, 30);
    EXPECT_TRUE(sim.PlayerHitProtected());
    const auto next = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_TRUE(next.playerProtected);
    EXPECT_EQ(sim.PlayerStats().hp, 30);
}

// R48 (bullets and spikes share the one gate): an enemy that fires every 0.1 s at point-blank range hits the hero at most once
// per 0.5 s; with the test switch off (the pre-R48 rule) it lands more often. A spike check right after a bullet hit is swallowed,
// and bullets inside the window after a spike hit fly on.
TEST(SpikeTrapTest, BulletsAndSpikesShareTheHalfSecondProtection) {
    const auto setup = [](Game::Sim::Simulation &sim, int hp) {
        sim.SetPlayerStats(Vitals(hp, 0));
        Game::EnemyDef def{};
        def.shootCd = 0.1F;
        def.scoutRate = 99.0F; // hold still
        def.friction = 0.9F;
        sim.AddEnemy(def, glm::vec2{30.0F, 0.0F}, /*roomId=*/0, /*seed=*/2);
    };
    const auto input = [] {
        Game::Sim::WorldInputs in;
        in.playerPos = {0.0F, 0.0F};
        in.aimDir = {1.0F, 0.0F};
        in.playerRoomId = 0;
        return in;
    };
    // (1) spacing of the damage events
    const auto spacing = [&](bool protection) {
        Game::Sim::Simulation sim(20240607, nullptr);
        sim.SetHitProtectionEnabled(protection);
        setup(sim, 1000);
        std::vector<int> stepsOfHits;
        int last = sim.PlayerStats().hp;
        for (int i = 0; i < 400; ++i) {
            sim.Advance(20.0F, input());
            if (sim.PlayerStats().hp < last) {
                stepsOfHits.push_back(i);
                last = sim.PlayerStats().hp;
            }
        }
        return stepsOfHits;
    };
    const std::vector<int> on = spacing(true);
    const std::vector<int> off = spacing(false);
    ASSERT_GE(on.size(), 3u);
    for (std::size_t i = 1; i < on.size(); ++i) {
        EXPECT_GE(on[i] - on[i - 1], 25) << "two bullet hits at least 0.5 s (25 fixed steps) apart";
    }
    int minGapOff = 1000;
    for (std::size_t i = 1; i < off.size(); ++i) {
        minGapOff = std::min(minGapOff, off[i] - off[i - 1]);
    }
    EXPECT_LT(minGapOff, 25) << "without the protection (pre-R48 rule) hits land closer than 0.5 s";
    EXPECT_GT(off.size(), on.size());
    // (2) bullet hit, then a spike check inside the window: swallowed
    {
        Game::Sim::Simulation sim(20240607, nullptr);
        setup(sim, 1000);
        int steps = 0;
        while (!sim.PlayerHitProtected() && steps++ < 200) {
            sim.Advance(20.0F, input());
        }
        ASSERT_TRUE(sim.PlayerHitProtected()) << "a bullet reached the hero";
        const int hp = sim.PlayerStats().hp;
        const auto r = sim.ApplyHazardBox({0.0F, 0.0F}, glm::vec2{16.0F, 16.0F}, 2, 1, true, true, false);
        EXPECT_TRUE(r.playerProtected);
        EXPECT_FALSE(r.player);
        EXPECT_EQ(sim.PlayerStats().hp, hp);
    }
    // (3) spike hit first, then bullets inside the window fly on; the first bullet after it lands
    {
        Game::Sim::Simulation sim(20240607, nullptr);
        setup(sim, 1000);
        sim.Advance(20.0F, input());
        const int hp0 = sim.PlayerStats().hp; // a bullet may already have landed: measure from here
        // protect-free start: use the switch-free path -- wait until unprotected, then spike
        int guard = 0;
        while (sim.PlayerHitProtected() && guard++ < 100) {
            sim.Advance(20.0F, input());
        }
        const int before = sim.PlayerStats().hp;
        (void)hp0;
        EXPECT_TRUE(sim.ApplyHazardBox({0.0F, 0.0F}, glm::vec2{16.0F, 16.0F}, 2, 1, true, true, false).player);
        EXPECT_EQ(sim.PlayerStats().hp, before - 2);
        for (int i = 0; i < 24; ++i) {
            sim.Advance(20.0F, input());
            EXPECT_EQ(sim.PlayerStats().hp, before - 2) << "step " << i << ": bullets pass the protected hero";
        }
        bool landed = false;
        for (int i = 0; i < 20 && !landed; ++i) {
            sim.Advance(20.0F, input());
            landed = sim.PlayerStats().hp < before - 2;
        }
        EXPECT_TRUE(landed) << "once the protection ends the next bullet hits";
    }
}

// R49/R50: a hit has FOUR outcomes and only Damaged is an effective hit: Damaged (armor / hp dropped, starts the 0.5 s protection),
// Protected (swallowed by it), Absorbed (the C08 shield ate it: nothing lost, no protection -- C08Controller$$GetHurt
// with in_skill only subtracts shield_value and never reaches RGController.GetHurt, tail truncated: INFERRED, T-77) and
// NoDamage (a zero-damage hit that reached GetHurt: nothing lost, still starts the protection, INFERRED T-77). The hazard result reports the real category, never "protected" for a non-damage.
TEST(SpikeTrapTest, PlainDamageIsReportedDamagedAndStartsTheProtection) {
    Game::Sim::Simulation sim(1, nullptr);
    sim.SetPlayerStats(Vitals(30, 0));
    sim.Advance(20.0F, InputAt({0.0F, 0.0F}));
    const auto r = sim.ApplyHazardBox({0.0F, 0.0F}, glm::vec2{16.0F, 16.0F}, 2, 1, true, true, false);
    EXPECT_TRUE(r.player);
    EXPECT_FALSE(r.playerProtected);
    EXPECT_FALSE(r.playerAbsorbed);
    EXPECT_FALSE(r.playerNoDamage);
    EXPECT_EQ(sim.PlayerStats().hp, 28);
    EXPECT_TRUE(sim.PlayerHitProtected());
}

namespace {
// A c08 hero whose shield is up (cast on the first step), standing at the origin.
void RaiseC08Shield(Game::Sim::Simulation &sim, int hp, int armor) {
    sim.SetPlayerStats(Vitals(hp, armor));
    sim.SetPlayerSkill("c08", /*skillCd=*/12.0F, /*inSkillTime=*/5.0F, /*atk=*/4);
    Game::Sim::WorldInputs cast = InputAt({0.0F, 0.0F});
    cast.skill = true;
    sim.Advance(20.0F, cast);
}
} // namespace

TEST(SpikeTrapTest, C08ShieldAbsorbingASpikeIsNotAnEffectiveHit) {
    Game::Sim::Simulation sim(9, nullptr);
    RaiseC08Shield(sim, 1, 8);
    ASSERT_TRUE(sim.PlayerSkill()->InSkill());
    const glm::vec2 half{16.0F, 16.0F};
    const auto r = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_TRUE(r.playerAbsorbed);
    EXPECT_FALSE(r.player) << "an absorbed check is not an effective hit";
    EXPECT_FALSE(r.playerProtected) << "and it is not 'protected' either";
    EXPECT_EQ(sim.PlayerStats().hp, 1);
    EXPECT_EQ(sim.PlayerStats().armor, 8);
    EXPECT_FALSE(sim.PlayerHitProtected()) << "an absorbed hit starts no protection";
    // the shield keeps eating every check (no can_hurt gate in the shield branch)
    const auto r2 = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_TRUE(r2.playerAbsorbed);
    EXPECT_FALSE(r2.playerProtected);
}

TEST(SpikeTrapTest, AbsorbedThenSpikeWithinHalfASecondStillDamagesOnceTheShieldIsDown) {
    Game::Sim::Simulation sim(9, nullptr);
    RaiseC08Shield(sim, 10, 0);
    const glm::vec2 half{16.0F, 16.0F};
    EXPECT_TRUE(sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false).playerAbsorbed);
    sim.Advance(20.0F, InputAt({0.0F, 0.0F})); // release the skill button (a toggle needs a fresh press edge)
    Game::Sim::WorldInputs endCast = InputAt({0.0F, 0.0F});
    endCast.skill = true; // toggle the shield off (C08-END)
    sim.Advance(20.0F, endCast);
    ASSERT_FALSE(sim.PlayerSkill()->InSkill());
    // ~60 ms after the absorb: still no protection, so this check is an effective hit ...
    const auto d = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_TRUE(d.player);
    EXPECT_FALSE(d.playerAbsorbed);
    EXPECT_EQ(sim.PlayerStats().hp, 8);
    // ... and that one DOES start the protection: the next check inside 0.5 s is Protected, not Absorbed or Damaged
    const auto p = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_TRUE(p.playerProtected);
    EXPECT_FALSE(p.player);
    EXPECT_FALSE(p.playerAbsorbed);
    EXPECT_EQ(sim.PlayerStats().hp, 8);
}

TEST(SpikeTrapTest, ProtectionComesBeforeTheShield) {
    // damaged first (shield down), shield up afterwards, a check inside the window: the hurtbox is off, nothing reaches the shield
    Game::Sim::Simulation sim(9, nullptr);
    sim.SetPlayerStats(Vitals(10, 0));
    sim.SetPlayerSkill("c08", 12.0F, 5.0F, 4);
    sim.Advance(20.0F, InputAt({0.0F, 0.0F}));
    const glm::vec2 half{16.0F, 16.0F};
    ASSERT_TRUE(sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false).player);
    Game::Sim::WorldInputs cast = InputAt({0.0F, 0.0F});
    cast.skill = true;
    sim.Advance(20.0F, cast);
    ASSERT_TRUE(sim.PlayerSkill()->InSkill());
    const auto r = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_TRUE(r.playerProtected);
    EXPECT_FALSE(r.playerAbsorbed);
}

TEST(SpikeTrapTest, ArmorAbsorbsTheTickBeforeHp) {
    Game::Sim::Simulation sim(1, nullptr);
    sim.SetPlayerStats(Vitals(7, 5));
    sim.Advance(20.0F, InputAt({0.0F, 0.0F}));
    sim.ApplyHazardBox({0.0F, 0.0F}, {16.0F, 16.0F}, 2, 1, true, true, false);
    EXPECT_EQ(sim.PlayerStats().armor, 3);
    EXPECT_EQ(sim.PlayerStats().hp, 7);
}

TEST(SpikeTrapTest, HazardBoxRespectsPositionFlagsImmunityAndDeadPlayers) {
    Game::Sim::Simulation sim(1, nullptr);
    sim.SetPlayerStats(Vitals(20, 0));
    sim.Advance(20.0F, InputAt({0.0F, 0.0F}));
    const glm::vec2 half{16.0F, 16.0F};
    // far away: nothing
    auto r = sim.ApplyHazardBox({400.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_FALSE(r.player);
    EXPECT_EQ(sim.PlayerStats().hp, 20);
    // hit_player false / immune (RGGameProcess.buff_immune_sting): the whole player branch is skipped
    r = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, false, true, false);
    EXPECT_FALSE(r.player);
    r = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, /*playerImmune=*/true);
    EXPECT_FALSE(r.player);
    EXPECT_EQ(sim.PlayerStats().hp, 20);
    // overlapping: damaged; the box edge counts (hurt radius is the placeholder circle, touching at the rim)
    r = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_TRUE(r.player);
    EXPECT_EQ(sim.PlayerStats().hp, 18);
    // a player that is not alive is not hit
    Game::Sim::WorldInputs dead = InputAt({0.0F, 0.0F});
    dead.playerAlive = false;
    sim.Advance(20.0F, dead);
    r = sim.ApplyHazardBox({0.0F, 0.0F}, half, 2, 1, true, true, false);
    EXPECT_FALSE(r.player);
    EXPECT_EQ(sim.PlayerStats().hp, 18);
}

TEST(SpikeTrapTest, EnemiesInTheBoxTakeHalfAndTheDeadAreSkipped) {
    Game::Sim::Simulation sim(3, nullptr);
    Game::EnemyDef def{};
    def.shootCd = 0.5F;
    def.scoutRate = 0.1F;
    def.friction = 0.9F;
    sim.AddEnemy(def, glm::vec2{0.0F, 0.0F}, /*roomId=*/2, 9);    // in the box
    sim.AddEnemy(def, glm::vec2{300.0F, 0.0F}, /*roomId=*/2, 10); // far away
    sim.SetPlayerStats(Vitals(20, 0));
    sim.Advance(20.0F, InputAt({-400.0F, 0.0F}));
    const int hp0 = sim.EnemyViews()[0].hp;
    const int hpFar = sim.EnemyViews()[1].hp;
    auto r = sim.ApplyHazardBox({0.0F, 0.0F}, glm::vec2{16.0F, 16.0F}, 2, 1, true, true, false);
    EXPECT_EQ(r.enemies, 1);
    EXPECT_FALSE(r.player);
    EXPECT_EQ(sim.EnemyViews()[0].hp, hp0 - 1);
    EXPECT_EQ(sim.EnemyViews()[1].hp, hpFar);
    // hit_enemy false: untouched
    r = sim.ApplyHazardBox({0.0F, 0.0F}, glm::vec2{16.0F, 16.0F}, 2, 1, true, false, false);
    EXPECT_EQ(r.enemies, 0);
    EXPECT_EQ(sim.EnemyViews()[0].hp, hp0 - 1);
    // enough ticks kill it; a dead enemy is then skipped (Cull marks it dead on the next step)
    for (int i = 0; i < 400; ++i) {
        sim.ApplyHazardBox({0.0F, 0.0F}, glm::vec2{16.0F, 16.0F}, 2, 1, false, true, false);
        sim.Advance(20.0F, InputAt({-400.0F, 0.0F}));
        if (!sim.EnemyViews()[0].alive) {
            break;
        }
    }
    EXPECT_FALSE(sim.EnemyViews()[0].alive);
    r = sim.ApplyHazardBox({0.0F, 0.0F}, glm::vec2{16.0F, 16.0F}, 2, 1, false, true, false);
    EXPECT_EQ(r.enemies, 0);
}

// NOLINTEND(readability-magic-numbers)
