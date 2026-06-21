#ifndef GAME_SIM_WEAPONBRAINADAPTERS_HPP
#define GAME_SIM_WEAPONBRAINADAPTERS_HPP

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include "combat/Gun001.hpp"
#include "combat/Gun002.hpp"
#include "combat/Gun004.hpp"
#include "combat/Gun005.hpp"
#include "combat/Gun006Paw.hpp"
#include "combat/Gun007.hpp"
#include "combat/Gun008.hpp"
#include "combat/Gun009.hpp"
#include "combat/Gun011.hpp"
#include "combat/Gun012.hpp"
#include "combat/Gun013.hpp"
#include "combat/Gun014.hpp"
#include "combat/Gun016.hpp"
#include "combat/Gun017.hpp"
#include "combat/Gun018.hpp"
#include "combat/Gun019.hpp"
#include "combat/GunHeroBow.hpp"
#include "combat/GunMagicBow.hpp"
#include "combat/GunStaffWizard.hpp"
#include "combat/GunThrow.hpp"
#include "combat/GunWaken.hpp"
#include "data/GameData.hpp"
#include "sim/IWeaponBrain.hpp"
#include "sim/SimMath.hpp"

namespace Game::Sim {

// === (d) per-gun weapon adapters (B1-P3). The gun-brain SOURCE is NEVER modified;
// each adapter only CALLS public brain methods and translates that gun's fire
// signature into IWeaponBrain::Tick. Each single-class gun encodes its OWN
// FirePattern, so the adapter returns the REAL pattern (vs the boss Fan-approx).
// Scatter RNG is drawn HERE and baked into FireIntent.dir -> FireSystem stays
// RNG-free. Burst/Charge are multi-tick: the adapter holds the state and emits one
// SINGLE sub-shot per fixed step (Burst) or one Charge release intent (Charge);
// only Charge tags FirePattern::Charge so FireSystem scales its speed. =========

/// Build the per-intent template (origin + bullet attributes) from the tick ctx.
inline FireIntent MakeBaseIntent(const IWeaponBrain::FireContext &c) {
    FireIntent fi;
    fi.origin = c.origin;
    fi.speedPxPerSec = c.bulletSpeedPxPerSec;
    fi.lifeMs = c.lifeMs;
    fi.damage = c.damage;
    fi.critical = c.critical;
    fi.repel = c.repel;
    fi.canThrough = c.canThrough;
    fi.pierce = c.pierce;
    fi.camp = c.camp;
    return fi;
}

/// Push one Single bullet at `scatterDeg` off the aim.
inline void EmitSingle(const IWeaponBrain::FireContext &c, std::vector<FireIntent> &out,
                       float scatterDeg) {
    FireIntent fi = MakeBaseIntent(c);
    fi.pattern = FirePattern::Single;
    fi.dir = RotateDeg(Normalize(c.aim), scatterDeg);
    out.push_back(fi);
}

// ---------------------------------------------------------------------------
// SINGLE -- one bullet at a recoil-widened scatter angle (one RNG draw).
// ---------------------------------------------------------------------------

class Gun001Adapter : public WeaponBrainBase<Game::Gun001> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire) { return 0; }
        EmitSingle(c, out, m_Brain.ScatterAngle(c.baseAngle, c.recoil));
        return 1;
    }
};

class Gun009Adapter : public WeaponBrainBase<Game::Gun009> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire) { return 0; }
        EmitSingle(c, out, m_Brain.RollScatter(c.baseAngle, c.recoil));
        return 1;
    }
};

/// Gun011 -- Single; its trigger-release end-shoot pellet is a separate owner-timed
/// pull (not modelled here -- debt). The primary pull is the canonical scatter.
class Gun011Adapter : public WeaponBrainBase<Game::Gun011> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire) { return 0; }
        EmitSingle(c, out, m_Brain.Attack(c.baseAngle, c.recoil));
        return 1;
    }
};

class Gun013Adapter : public WeaponBrainBase<Game::Gun013> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire) { return 0; }
        EmitSingle(c, out, m_Brain.Scatter(c.baseAngle, c.recoil));
        return 1;
    }
};

/// Gun016 -- heat/spin-up minigun. Heat accrues per fixed step while firing (the
/// adapter owns it, mirroring the legacy WeaponController heat), tightening the
/// cone toward a straight shot; one symmetric draw per shot.
class Gun016Adapter : public WeaponBrainBase<Game::Gun016> {
public:
    float HeatTime() const override { return m_Heat; }
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (Game::Gun016::ShouldTickHeat(c.firing, m_Heat, Game::Gun016::kShootMaxTime)) {
            m_Heat += c.fixedStepSeconds;
        } else if (!c.firing) {
            m_Heat = (std::max)(0.0F, m_Heat - c.fixedStepSeconds);
        }
        if (!c.firing || !c.canFire) { return 0; }
        const float ratio = Game::Gun016::HeatRatio(m_Heat, Game::Gun016::kShootMaxTime);
        const float spread = Game::Gun016::Spread(c.baseAngle, c.recoil, ratio);
        EmitSingle(c, out, m_Brain.ScatterAngle(spread));
        return 1;
    }

private:
    float m_Heat = 0.0F;
};

/// Gun017 -- Single (drone-parent). The deploy/retract drone state machine is
/// owner-gated (RGController bools) -> not modelled; the parent always fires (debt).
class Gun017Adapter : public WeaponBrainBase<Game::Gun017> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire) { return 0; }
        const float spread = Game::Gun017::BaseSpreadWithRecoil(c.baseAngle, c.recoil);
        EmitSingle(c, out, m_Brain.ScatterAngle(spread));
        return 1;
    }
};

/// GunStaffWizard -- Single; the 4-phase counter / sign-gated early-out are
/// owner-gated -> not modelled (debt). The firing branch is canonical scatter.
class GunStaffWizardAdapter : public WeaponBrainBase<Game::GunStaffWizard> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire) { return 0; }
        const float spread = Game::GunStaffWizard::SpreadHalfAngle(c.baseAngle, c.recoil);
        EmitSingle(c, out, m_Brain.ScatterAngle(spread));
        return 1;
    }
};

/// GunWaken -- Single (normal mode). The awakened zero-scatter mode is owner-gated
/// (owner+0x84 flag) -> normal scatter modelled (debt).
class GunWakenAdapter : public WeaponBrainBase<Game::GunWaken> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire) { return 0; }
        const float spread = Game::GunWaken::SpreadHalfAngle(c.baseAngle, c.recoil);
        EmitSingle(c, out, m_Brain.ScatterAngle(spread));
        return 1;
    }
};

// ---------------------------------------------------------------------------
// FAN -- N pre-rotated Single intents per pull (one draw per pellet); the fan
// geometry uses the recovered fan-start helpers + the data step angle.
// ---------------------------------------------------------------------------

class Gun002Adapter : public WeaponBrainBase<Game::Gun002> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire || !Game::Gun002::WillFire(c.count)) { return 0; }
        const float halfSpan = Game::Gun002::SpreadHalfSpan(c.baseAngle, c.recoil);
        const glm::vec2 aim = Normalize(c.aim);
        for (int p = 0; p < c.count; ++p) {
            const float base = Game::Gun002::PelletBaseAngle(c.count, p, c.stepAngle);
            const float scatter = m_Brain.ScatterPellet(halfSpan);
            FireIntent fi = MakeBaseIntent(c);
            fi.pattern = FirePattern::Single;
            fi.dir = RotateDeg(aim, base + scatter);
            out.push_back(fi);
        }
        return 1;
    }
};

/// Gun012 -- Gun002-sibling fan. Constructed with (count, scatterBase=deviation) so
/// its ScatterAngle(recoil) uses the right scatter base; the fan step is the data
/// angle. One draw per pellet.
class Gun012Adapter : public IWeaponBrain {
public:
    Gun012Adapter(int count, float scatterBase) : m_Brain(count, scatterBase) {}
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire || c.count < 1) { return 0; }
        const glm::vec2 aim = Normalize(c.aim);
        for (int p = 0; p < c.count; ++p) {
            const float base =
                static_cast<float>(Game::Gun012::FanStartFor(c.count) + p) * c.stepAngle;
            const float scatter = m_Brain.ScatterAngle(c.recoil); // ScatterHalfAngle+draw
            FireIntent fi = MakeBaseIntent(c);
            fi.pattern = FirePattern::Single;
            fi.dir = RotateDeg(aim, base + scatter);
            out.push_back(fi);
        }
        return 1;
    }

private:
    Game::Gun012 m_Brain;
};

/// Gun014 -- multi-barrel fan + per-pellet scatter. The rotating base-angle re-roll
/// (AdjustAngle) divisor is owner-fed/unrecovered -> not applied (debt).
class Gun014Adapter : public WeaponBrainBase<Game::Gun014> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire || !Game::Gun014::CanCreateBullet(c.count)) { return 0; }
        const float halfWidth = Game::Gun014::SpreadWithRecoil(c.baseAngle, c.recoil);
        const glm::vec2 aim = Normalize(c.aim);
        for (int p = 0; p < c.count; ++p) {
            const float base =
                static_cast<float>(Game::Gun014::FanStartIndex(c.count) + p) * c.stepAngle;
            const float scatter = m_Brain.ScatterDraw(halfWidth);
            FireIntent fi = MakeBaseIntent(c);
            fi.pattern = FirePattern::Single;
            fi.dir = RotateDeg(aim, base + scatter);
            out.push_back(fi);
        }
        return 1;
    }
};

/// Gun018 -- fixed 3-pellet shotgun (ctor-immediate kBulletCount); one AttackScatter
/// draw per pellet. The secondary per-pellet CreateBullet counter path is owner-driven
/// -> only the Attack-stage scatter is modelled (debt).
class Gun018Adapter : public WeaponBrainBase<Game::Gun018> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire) { return 0; }
        const int n = m_Brain.BulletCount();
        const float step = c.stepAngle;
        const glm::vec2 aim = Normalize(c.aim);
        for (int p = 0; p < n; ++p) {
            const float base = (static_cast<float>(p) - static_cast<float>(n - 1) * 0.5F) * step;
            const float scatter = m_Brain.AttackScatter(c.baseAngle, c.recoil);
            FireIntent fi = MakeBaseIntent(c);
            fi.pattern = FirePattern::Single;
            fi.dir = RotateDeg(aim, base + scatter);
            out.push_back(fi);
        }
        return 1;
    }
};

/// GunThrow -- deterministic geometric throw fan (ZERO RNG). The >=6-bullet spread
/// magnitudes are unrecoverable DAT_ globals (return 0) -> degraded for large fans (debt).
class GunThrowAdapter : public WeaponBrainBase<Game::GunThrow> {
public:
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!c.firing || !c.canFire || c.count < 1) { return 0; }
        const glm::vec2 aim = Normalize(c.aim);
        for (int p = 0; p < c.count; ++p) {
            const float angle = Game::GunThrow::GetShootAngle(c.count, p, c.baseAngle);
            FireIntent fi = MakeBaseIntent(c);
            fi.pattern = FirePattern::Single;
            fi.dir = RotateDeg(aim, angle);
            out.push_back(fi);
        }
        return 1;
    }
};

// ---------------------------------------------------------------------------
// BURST -- the adapter pumps the gun's salvo, emitting one Single sub-shot per
// fixed step across ticks (decision B). Inter-shot spacing is 1/tick (the
// recovered has_delay/max_delay is loader-dropped -> placeholder, debt).
// ---------------------------------------------------------------------------

/// Gun004 -- fixed-size salvo via ShouldFireShot pump.
class Gun004Adapter : public IWeaponBrain {
public:
    explicit Gun004Adapter(int burstCount) : m_Brain(burstCount) {}
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!m_InBurst) {
            const bool rising = c.firing && !m_WasFiring;
            m_WasFiring = c.firing;
            if (!rising) { return 0; }
            m_Brain.BeginBurst();
            m_InBurst = true;
        } else {
            m_WasFiring = c.firing;
        }
        if (!m_Brain.ShouldFireShot(/*interrupted=*/false)) {
            m_InBurst = false;
            return 0;
        }
        EmitSingle(c, out, m_Brain.ShotScatterAngle(Game::Gun004::ShotSpread(c.baseAngle, c.recoil)));
        return 1;
    }

private:
    Game::Gun004 m_Brain;
    bool m_InBurst = false;
    bool m_WasFiring = false;
};

/// Gun008 -- coroutine laser-sweep of `limit` ticks (Gun008SweepIterator).
class Gun008Adapter : public IWeaponBrain {
public:
    explicit Gun008Adapter(int limit) : m_Limit(limit) {}
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!m_It.has_value()) {
            const bool rising = c.firing && !m_WasFiring;
            m_WasFiring = c.firing;
            if (!rising) { return 0; }
            m_It.emplace(m_Limit);
        } else {
            m_WasFiring = c.firing;
        }
        const bool alive = m_It->MoveNext();
        int pulls = 0;
        if (m_It->Fired()) {
            const float half = Game::Gun008::SweepHalfAngle(c.baseAngle, c.recoil);
            EmitSingle(c, out, m_Brain.SweepScatterAngle(half));
            pulls = 1;
        }
        if (!alive) { m_It.reset(); }
        return pulls;
    }

private:
    Game::Gun008 m_Brain;
    std::optional<Game::Gun008SweepIterator> m_It;
    int m_Limit;
    bool m_WasFiring = false;
};

/// Gun019 -- Invoke-rescheduled chain of single shots (BurstAdvance).
class Gun019Adapter : public IWeaponBrain {
public:
    explicit Gun019Adapter(int burstCount) : m_Count(burstCount) {}
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (!m_InBurst) {
            const bool rising = c.firing && !m_WasFiring;
            m_WasFiring = c.firing;
            if (!rising) { return 0; }
            m_Index = 0;
            m_InBurst = true;
        } else {
            m_WasFiring = c.firing;
        }
        EmitSingle(c, out, m_Brain.ScatterAngle(Game::Gun019::SpreadHalfAngle(c.baseAngle, c.recoil)));
        const Game::Gun019::BurstStep step = Game::Gun019::BurstAdvance(m_Index, m_Count);
        m_Index = step.index;
        if (!step.reschedule) { m_InBurst = false; }
        return 1;
    }

private:
    Game::Gun019 m_Brain;
    int m_Count;
    int m_Index = 0;
    bool m_InBurst = false;
    bool m_WasFiring = false;
};

/// Gun007 -- CHARGE+BURST hybrid: hold accrues charge; on release the charge sets
/// the burst size (BulletCount), fired over ticks (Gun007BurstIterator). Each
/// sub-shot is a Charge intent whose speed scales by the locked-in charge ratio.
class Gun007Adapter : public IWeaponBrain {
public:
    Gun007Adapter(float maxTime, int maxCount)
        : m_MaxTime(maxTime > 0.0F ? maxTime : 1.0F), // max_time fallback (debt: owner cap)
          m_MaxCount(maxCount > 0 ? maxCount : 1) {}
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (c.firing) {
            m_ATime += c.fixedStepSeconds;
            m_WasFiring = true;
            return 0;
        }
        if (!m_It.has_value() && m_WasFiring && m_ATime > 0.0F) {
            m_WasFiring = false;
            m_Ratio = Game::Gun007::ChargeRatio(m_ATime, m_MaxTime);
            const int count = Game::Gun007::BulletCount(m_MaxCount, m_Ratio);
            m_ATime = 0.0F;
            if (count <= 0) { return 0; }
            m_It.emplace(count);
        }
        m_WasFiring = c.firing;
        if (!m_It.has_value()) { return 0; }
        const bool alive = m_It->MoveNext();
        int pulls = 0;
        if (m_It->Fired()) {
            FireIntent fi = MakeBaseIntent(c);
            fi.pattern = FirePattern::Charge;
            fi.dir = Normalize(c.aim);
            fi.chargeRatio = m_Ratio;
            out.push_back(fi);
            pulls = 1;
        }
        if (!alive) { m_It.reset(); }
        return pulls;
    }

private:
    Game::Gun007 m_Brain;
    std::optional<Game::Gun007BurstIterator> m_It;
    float m_MaxTime;
    int m_MaxCount;
    float m_ATime = 0.0F;
    float m_Ratio = 0.0F;
    bool m_WasFiring = false;
};

// ---------------------------------------------------------------------------
// CHARGE -- hold accrues charge; release emits a FirePattern::Charge intent whose
// speed FireSystem scales by chargeRatio. ZERO RNG.
// ---------------------------------------------------------------------------

class Gun005Adapter : public IWeaponBrain {
public:
    explicit Gun005Adapter(float maxCharge) : m_Brain(maxCharge) {}
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (c.firing) {
            m_Brain.AddCharge(c.fixedStepSeconds);
            m_WasFiring = true;
            return 0;
        }
        if (m_WasFiring && m_Brain.Charge() > 0.0F) {
            m_WasFiring = false;
            FireIntent fi = MakeBaseIntent(c);
            fi.pattern = FirePattern::Charge;
            fi.dir = Normalize(c.aim);
            fi.chargeRatio = m_Brain.ChargeRatio();
            m_Brain.ConsumeCharge();
            out.push_back(fi);
            return 1;
        }
        m_WasFiring = false;
        return 0;
    }

private:
    Game::Gun005 m_Brain;
    bool m_WasFiring = false;
};

/// GunHeroBow -- charged multi-arrow bow: release fires `arrowCount` arrows over
/// ticks (ShouldFireNext), each a Charge intent (speed scaled by the charge ratio).
class GunHeroBowAdapter : public IWeaponBrain {
public:
    GunHeroBowAdapter(float maxCharge, int arrowCount)
        : m_MaxCharge(maxCharge > 0.0F ? maxCharge : 1.0F),
          m_ArrowCount(arrowCount > 0 ? arrowCount : 1) {}
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (c.firing) {
            m_Charge += c.fixedStepSeconds;
            m_WasFiring = true;
            return 0;
        }
        if (!m_Releasing && m_WasFiring && Game::GunHeroBow::HasArrows(m_ArrowCount)) {
            m_WasFiring = false;
            m_Ratio = Game::GunHeroBow::ChargeRatio(m_Charge, m_MaxCharge);
            m_Charge = 0.0F;
            m_Releasing = true;
            m_FiredIndex = 0;
        }
        m_WasFiring = c.firing;
        if (!m_Releasing) { return 0; }
        FireIntent fi = MakeBaseIntent(c);
        fi.pattern = FirePattern::Charge;
        fi.dir = Normalize(c.aim);
        fi.chargeRatio = m_Ratio;
        out.push_back(fi);
        if (!Game::GunHeroBow::ShouldFireNext(m_FiredIndex, m_ArrowCount)) {
            m_Releasing = false;
        }
        ++m_FiredIndex;
        return 1;
    }

private:
    Game::GunHeroBow m_Brain;
    float m_MaxCharge;
    int m_ArrowCount;
    float m_Charge = 0.0F;
    float m_Ratio = 0.0F;
    int m_FiredIndex = 0;
    bool m_Releasing = false;
    bool m_WasFiring = false;
};

/// GunMagicBow -- charged single shot (all-axis velocity scale). Charge accrues
/// only while firing && charge<maxCharge (ShouldTickCharge gate).
class GunMagicBowAdapter : public IWeaponBrain {
public:
    explicit GunMagicBowAdapter(float maxCharge)
        : m_MaxCharge(maxCharge > 0.0F ? maxCharge : 1.0F) {}
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }
    int Tick(const FireContext &c, std::vector<FireIntent> &out) override {
        if (Game::GunMagicBow::ShouldTickCharge(c.firing, m_Charge, m_MaxCharge)) {
            m_Charge += c.fixedStepSeconds;
        }
        if (c.firing) {
            m_WasFiring = true;
            return 0;
        }
        if (m_WasFiring && m_Charge > 0.0F) {
            m_WasFiring = false;
            FireIntent fi = MakeBaseIntent(c);
            fi.pattern = FirePattern::Charge;
            fi.dir = Normalize(c.aim);
            fi.chargeRatio = Game::GunMagicBow::ChargeRatio(m_Charge, m_MaxCharge);
            m_Charge = 0.0F;
            out.push_back(fi);
            return 1;
        }
        m_WasFiring = false;
        return 0;
    }

private:
    Game::GunMagicBow m_Brain;
    float m_MaxCharge;
    float m_Charge = 0.0F;
    bool m_WasFiring = false;
};

// ---------------------------------------------------------------------------
// SPECIAL -- non-bullet weapons.
// ---------------------------------------------------------------------------

/// Gun006 (Gun006Paw) -- companion-sword stat buff; fires NO bullet. Registered so
/// equip dispatches, but Tick never emits (the buff is owner/melee-side, out of the
/// bullet-pattern scope).
class Gun006PawAdapter : public WeaponBrainBase<Game::Gun006Paw> {
public:
    int Tick(const FireContext & /*c*/, std::vector<FireIntent> & /*out*/) override {
        return 0;
    }
};

/// Dispatch a weapon id -> its (d) brain adapter. All 21 data-backed guns are
/// registered, each returning its real pattern. Orphan Gun013 (no weapons.json row)
/// is registered but loot-unreachable (debt). GunMultiBullet has no fire body of its
/// own -- its fire delegates to Gun019's shape, so it maps to Gun019Adapter (per-bullet
/// stat overrides not applied, debt). Unknown id -> Gun001Adapter (safe default).
inline std::unique_ptr<IWeaponBrain> MakeWeaponBrain(const std::string &weaponId,
                                                     const Game::WeaponDef &def) {
    const int count = def.count > 0 ? def.count : 1;
    // --- Single ---
    if (weaponId == "Gun009") { return std::make_unique<Gun009Adapter>(); }
    if (weaponId == "Gun011") { return std::make_unique<Gun011Adapter>(); }
    if (weaponId == "Gun013") { return std::make_unique<Gun013Adapter>(); }
    if (weaponId == "Gun016") { return std::make_unique<Gun016Adapter>(); }
    if (weaponId == "Gun017") { return std::make_unique<Gun017Adapter>(); }
    if (weaponId == "GunStaffWizard") { return std::make_unique<GunStaffWizardAdapter>(); }
    if (weaponId == "GunWaken") { return std::make_unique<GunWakenAdapter>(); }
    // --- Fan ---
    if (weaponId == "Gun002") { return std::make_unique<Gun002Adapter>(); }
    if (weaponId == "Gun012") {
        return std::make_unique<Gun012Adapter>(count, static_cast<float>(def.deviation));
    }
    if (weaponId == "Gun014") { return std::make_unique<Gun014Adapter>(); }
    if (weaponId == "Gun018") { return std::make_unique<Gun018Adapter>(); }
    if (weaponId == "GunThrow") { return std::make_unique<GunThrowAdapter>(); }
    // --- Burst ---
    if (weaponId == "Gun004") { return std::make_unique<Gun004Adapter>(count); }
    if (weaponId == "Gun008") { return std::make_unique<Gun008Adapter>(count); }
    if (weaponId == "Gun019") { return std::make_unique<Gun019Adapter>(count); }
    if (weaponId == "Gun007") {
        return std::make_unique<Gun007Adapter>(def.maxTime, def.aCount);
    }
    // --- Charge ---
    if (weaponId == "Gun005") { return std::make_unique<Gun005Adapter>(Game::Gun005::kDefaultMaxCharge); }
    if (weaponId == "GunHeroBow") {
        return std::make_unique<GunHeroBowAdapter>(def.maxTime, def.aCount);
    }
    if (weaponId == "GunMagicBow") { return std::make_unique<GunMagicBowAdapter>(def.maxTime); }
    // --- Special ---
    if (weaponId == "Gun006") { return std::make_unique<Gun006PawAdapter>(); }
    if (weaponId == "GunMultiBullet") { return std::make_unique<Gun019Adapter>(count); }
    return std::make_unique<Gun001Adapter>(); // Gun001 + default fallback
}

} // namespace Game::Sim

#endif /* GAME_SIM_WEAPONBRAINADAPTERS_HPP */
