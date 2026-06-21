#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "combat/Gun001.hpp"
#include "combat/Gun002.hpp"
#include "data/GameData.hpp"
#include "data/RGRandom.hpp"
#include "sim/BulletState.hpp"
#include "sim/FireIntent.hpp"
#include "sim/FireSystem.hpp"
#include "sim/IWeaponBrain.hpp"
#include "sim/SimMath.hpp"
#include "sim/Simulation.hpp"
#include "sim/WeaponBrainAdapters.hpp"
#include "sim/WorldInputs.hpp"

using namespace Game::Sim;

// NOLINTBEGIN(readability-magic-numbers)

// B1-P3 3.1 smoke: the (d) IWeaponBrain layer + FireSystem Charge primitive. Proves
// FOUR heterogeneous guns each fire their OWN pattern shape (not all one bullet):
//   Gun001 Single (scatter) / Gun002 Fan (N pellets) / Gun004 Burst (across ticks) /
//   Gun005 Charge (held-accrue, release scales speed).
namespace {

float AngleDeg(glm::vec2 v) { return std::atan2(v.y, v.x) * 180.0F / 3.14159265358979F; }
float Mag(glm::vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

IWeaponBrain::FireContext Ctx(bool firing) {
    IWeaponBrain::FireContext c;
    c.firing = firing;
    c.origin = glm::vec2{0.0F, 0.0F};
    c.aim = glm::vec2{1.0F, 0.0F};
    c.fixedStepSeconds = 0.02F;
    c.bulletSpeedPxPerSec = 100.0F;
    c.lifeMs = 1500.0F;
    c.damage = 1;
    c.camp = 0;
    return c;
}

// Expand a brain's one-tick intents into bullets via the real FireSystem.
std::vector<BulletState> FireOnce(IWeaponBrain &brain, const IWeaponBrain::FireContext &c) {
    std::vector<FireIntent> intents;
    brain.Tick(c, intents);
    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;
    for (const auto &fi : intents) {
        fs.Expand(fi, nextId, out);
    }
    return out;
}

} // namespace

// --- Gun001: SINGLE, one scattered bullet, drawing Gun001's RNG exactly once ------
TEST(WeaponBrainSmoke, Gun001SingleScatterLockstep) {
    Gun001Adapter adapter;
    adapter.SetSeed(42);
    auto c = Ctx(true);
    c.baseAngle = 5.0F;
    c.recoil = 0.0F;

    const std::vector<BulletState> bullets = FireOnce(adapter, c);
    ASSERT_EQ(bullets.size(), 1U); // a single pull -> ONE bullet (not a fan/burst)

    // Lockstep: a parallel Gun001 seeded identically yields the same scatter draw.
    Game::Gun001 ref;
    ref.SetSeed(42);
    const float scatter = ref.ScatterAngle(5.0F, 0.0F);
    const glm::vec2 expectDir = RotateDeg(glm::vec2{1.0F, 0.0F}, scatter);
    EXPECT_NEAR(bullets[0].vel.x, expectDir.x * 100.0F, 1e-3F);
    EXPECT_NEAR(bullets[0].vel.y, expectDir.y * 100.0F, 1e-3F);
    EXPECT_NEAR(Mag(bullets[0].vel), 100.0F, 1e-3F); // speed unscaled (no charge)
}

// --- Gun002: FAN, `count` pellets at the geometric fan angles -----------------
TEST(WeaponBrainSmoke, Gun002FanEmitsCountPelletsAtFanAngles) {
    Gun002Adapter adapter;
    adapter.SetSeed(7);
    auto c = Ctx(true);
    c.baseAngle = 0.0F; // deviation 0 -> zero per-pellet scatter -> exact geometric fan
    c.recoil = 0.0F;
    c.count = 3;
    c.stepAngle = 15.0F;

    const std::vector<BulletState> bullets = FireOnce(adapter, c);
    ASSERT_EQ(bullets.size(), 3U); // FAN: three pellets (distinct from Single's one)
    EXPECT_NEAR(AngleDeg(bullets[0].vel), -15.0F, 1e-3F);
    EXPECT_NEAR(AngleDeg(bullets[1].vel), 0.0F, 1e-3F);
    EXPECT_NEAR(AngleDeg(bullets[2].vel), 15.0F, 1e-3F);

    // The fan drew the gun's RNG exactly ONCE PER PELLET (3 draws): a parallel brain
    // that draws 3 scatters then probes its stream must match the adapter's stream.
    Game::Gun002 ref;
    ref.SetSeed(7);
    for (int p = 0; p < 3; ++p) {
        (void)ref.ScatterPellet(Game::Gun002::SpreadHalfSpan(0.0F, 0.0F));
    }
    EXPECT_EQ(adapter.RngRange(0, 1000000), ref.Rng().Range(0, 1000000));
}

// --- Gun004: BURST, one sub-shot PER TICK across ticks (not a same-tick volley) --
TEST(WeaponBrainSmoke, Gun004BurstFiresOnePerTickAcrossTicks) {
    Gun004Adapter adapter(/*burstCount=*/4);
    adapter.SetSeed(1);
    auto c = Ctx(true);
    c.baseAngle = 3.0F;

    std::vector<int> perTick;
    for (int t = 0; t < 5; ++t) {
        const std::vector<BulletState> b = FireOnce(adapter, c);
        perTick.push_back(static_cast<int>(b.size()));
    }
    // Time-spaced salvo: exactly one bullet on each of the first ticks, then the
    // brain ends the burst. A same-tick volley would be {N,0,0,0}; this is {1,1,1,0,0}.
    EXPECT_EQ(perTick[0], 1);
    EXPECT_EQ(perTick[1], 1);
    EXPECT_EQ(perTick[2], 1);
    EXPECT_EQ(perTick[3], 0); // burstCount 4 -> brain ends on the 4th pump
    EXPECT_EQ(perTick[4], 0);
}

// --- Gun005: CHARGE, no shot while held; release scales bullet speed by ratio ----
TEST(WeaponBrainSmoke, Gun005ChargeAccruesThenReleasesScaledSpeed) {
    auto chargeAndRelease = [](int holdTicks) {
        Gun005Adapter adapter(/*maxCharge=*/2.0F);
        adapter.SetSeed(3);
        auto c = Ctx(true);
        // Hold the trigger: accrues charge, emits NOTHING each tick.
        for (int t = 0; t < holdTicks; ++t) {
            const std::vector<BulletState> mid = FireOnce(adapter, c);
            EXPECT_TRUE(mid.empty()) << "charge weapon must not fire while held";
        }
        // Release: one charge bullet whose speed = base * (charge/maxCharge).
        c.firing = false;
        return FireOnce(adapter, c);
    };

    const std::vector<BulletState> lo = chargeAndRelease(50);  // charge 1.0 -> ratio 0.5
    const std::vector<BulletState> hi = chargeAndRelease(100); // charge 2.0 -> ratio 1.0
    ASSERT_EQ(lo.size(), 1U);
    ASSERT_EQ(hi.size(), 1U);
    EXPECT_NEAR(Mag(lo[0].vel), 100.0F * 0.5F, 1e-2F);
    EXPECT_NEAR(Mag(hi[0].vel), 100.0F * 1.0F, 1e-2F);
    EXPECT_GT(Mag(hi[0].vel), Mag(lo[0].vel)); // more charge -> faster shot
}

// --- the FireSystem Charge branch takes ZERO RNG draws (purity invariant) --------
TEST(WeaponBrainSmoke, FireSystemChargeBranchDrawsNoRng) {
    Game::RGRandom ref;
    ref.SetRandomSeed(99);
    Game::RGRandom probe;
    probe.SetRandomSeed(99);

    FireSystem fs;
    std::vector<BulletState> out;
    std::uint32_t nextId = 1;
    for (int i = 0; i < 16; ++i) {
        FireIntent fi;
        fi.pattern = FirePattern::Charge;
        fi.dir = glm::vec2{1.0F, 0.0F};
        fi.speedPxPerSec = 100.0F;
        fi.chargeRatio = 0.75F;
        fs.Expand(fi, nextId, out);
        out.clear();
    }
    EXPECT_EQ(ref.Range(0, 1000000), probe.Range(0, 1000000)); // FireSystem stays RNG-free
}

// --- the (d) registry dispatches each id to a brain that fires its own pattern ----
TEST(WeaponBrainSmoke, MakeWeaponBrainDispatchesByid) {
    Game::WeaponDef def;
    def.count = 3;
    def.id = "Gun002";
    auto fan = MakeWeaponBrain("Gun002", def);
    fan->SetSeed(5);
    auto c = Ctx(true);
    c.count = 3;
    c.stepAngle = 15.0F;
    const std::vector<BulletState> b = FireOnce(*fan, c);
    EXPECT_EQ(b.size(), 3U); // dispatched to the Fan adapter (3 pellets)

    Game::WeaponDef single;
    single.id = "Gun001";
    auto one = MakeWeaponBrain("Gun001", single);
    one->SetSeed(5);
    auto c2 = Ctx(true);
    c2.baseAngle = 5.0F;
    const std::vector<BulletState> b2 = FireOnce(*one, c2);
    EXPECT_EQ(b2.size(), 1U); // dispatched to the Single adapter
}

// === End-to-end (d) dispatch through Simulation::EquipWeapon (pick + fire). Proves
// the full pipeline: EquipWeapon -> BrainFactory::MakeWeapon -> MakeWeaponBrain ->
// WeaponController(brain) -> Tick -> FireSystem. (Single is already covered by
// SimulationTest.EquippedWeaponFiresPlayerBulletsOnCadence via the Gun001 adapter.)
namespace {
Game::Sim::WorldInputs SimInput(bool firing) {
    Game::Sim::WorldInputs in;
    in.playerPos = glm::vec2{0.0F, 0.0F};
    in.aimDir = glm::vec2{1.0F, 0.0F};
    in.playerRoomId = 0;
    in.firing = firing;
    return in;
}
} // namespace

TEST(WeaponBrainIntegration, Gun002EquipFiresFanOnePullManyPellets) {
    Game::Sim::Simulation sim(/*runSeed=*/55, nullptr);
    Game::WeaponDef def{};
    def.bulletSpeed = 10.0F;
    def.atk = 2;
    def.count = 3;
    def.angle = 15.0F;
    def.deviation = 0;
    sim.EquipWeapon(def, "Gun002", 808);

    sim.Advance(20.0F, SimInput(true)); // one fixed step -> one pull
    EXPECT_EQ(sim.Bullets().size(), 3U);          // FAN: three pellets
    EXPECT_EQ(sim.PlayerShotsLastAdvance(), 1);   // energy is PER-PULL, not per-pellet
}

TEST(WeaponBrainIntegration, Gun004EquipFiresBurstAcrossTicksNotAtOnce) {
    Game::Sim::Simulation sim(/*runSeed=*/7, nullptr);
    Game::WeaponDef def{};
    def.bulletSpeed = 10.0F;
    def.count = 4; // burst size -> 3 sub-shots fire (brain ends on the 4th pump)
    def.deviation = 3;
    sim.EquipWeapon(def, "Gun004", 1);

    sim.Advance(20.0F, SimInput(true));
    EXPECT_EQ(sim.Bullets().size(), 1U); // time-spaced: ONE bullet on the first tick, not 3
    for (int i = 0; i < 6; ++i) {
        sim.Advance(20.0F, SimInput(true)); // hold; the salvo plays out over ticks
    }
    EXPECT_EQ(sim.Bullets().size(), 3U); // exactly the salvo, then it stops (held != re-trigger)
}

TEST(WeaponBrainIntegration, Gun005EquipChargesWhileHeldThenReleasesScaled) {
    Game::Sim::Simulation sim(/*runSeed=*/3, nullptr);
    Game::WeaponDef def{};
    def.bulletSpeed = 10.0F; // 150 px/s base (x kDataSpeedToPxPerSec)
    sim.EquipWeapon(def, "Gun005", 1);

    for (int i = 0; i < 100; ++i) {
        sim.Advance(20.0F, SimInput(true)); // accrue charge ~2.0 (full); fire NOTHING while held
    }
    EXPECT_TRUE(sim.Bullets().empty());

    sim.Advance(20.0F, SimInput(false)); // release -> one charge bullet at the accrued ratio
    ASSERT_EQ(sim.Bullets().size(), 1U);
    EXPECT_NEAR(Mag(sim.Bullets()[0].vel), 150.0F, 5.0F); // full charge -> full base speed
}

// NOLINTEND(readability-magic-numbers)
