#ifndef GAME_SIM_BOSSBRAINADAPTERS_HPP
#define GAME_SIM_BOSSBRAINADAPTERS_HPP

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "combat/BossAI01.hpp"
#include "combat/BossAI02.hpp"
#include "combat/BossAI03.hpp"
#include "combat/BossAI04.hpp"
#include "combat/BossAI05.hpp"
#include "combat/BossAI06.hpp"
#include "combat/BossAI07.hpp"
#include "combat/BossAI08.hpp"
#include "combat/BossAI09.hpp"
#include "combat/BossAI10.hpp"
#include "combat/BossAI11.hpp"
#include "combat/BossAI12.hpp"
#include "combat/BossAI13.hpp"
#include "combat/BossAI14.hpp"
#include "sim/BossController.hpp" // kFan* constants
#include "sim/IBossBrain.hpp"

namespace Game::Sim {

// === (d) per-boss adapters. The brain SOURCE is NEVER modified -- each adapter
// only CALLS public brain methods and translates that brain's heterogeneous tick
// signatures into IBossBrain. The 14 standard FIGHTING bosses BossAI01..BossAI14
// are registered in MakeBossBrain below.
//
// NOT registered (deferred special-mechanism sub-entities, see docs/LEVEL_GEN_PLAN.md):
//   * BossAI06Child  -- summon spawned by BossAI06 (the AI06 summon subsystem);
//   * BossAI12Parent -- HP-coordinator for BossAI12's two halves (composite-HP),
//                       has NO attack/move/RNG so it is not a fighting brain;
//   * BossAINian / BossAINianLantern -- event bosses with no standard def.
// For AI06/AI12 the FIGHTING brain (BossAI06/BossAI12) is registered and fires
// bullets; the summon / composite-HP mechanic is a no-op here (later stage).
//
// PATTERN/CADENCE FIDELITY (phase A, Fan-approx): every adapter returns a Fan
// (the roll->attack jumptable is owner-truncated for every boss) and opens the
// can_shoot gate each shoot tick so the boss fires on cadence -- the faithful
// per-attack pattern (patterns stage) and the anim-gated can_shoot cadence are
// recorded debt. ================================================================

/// Fan bullet count from an attack roll: even roll -> kFanEven, odd -> kFanOdd.
/// A deterministic, varied fan for the bosses that surface a roll (used only when
/// the shot fires).
inline int BossFanCount(int roll) {
    return (roll % 2 == 0) ? BossController::kFanEven : BossController::kFanOdd;
}

/// BossAI01 -- the byte-identical baseline. Standalone (BossAI01 has NO default
/// ctor -- it needs baseShootCd), forwards VERBATIM so the BossAI01 path matches
/// the pre-(d) controller exactly: ChooseAttack() (1 draw) -> Fan even/odd,
/// ChaseMoveDecision (2 draws), ShootCd() reschedule.
class BossAI01Adapter : public IBossBrain {
public:
    explicit BossAI01Adapter(float baseShootCd) : m_Brain(baseShootCd) {}
    void SetSeed(int seed) override { m_Brain.SetSeed(seed); }
    int RngRange(int lo, int hi) override { return m_Brain.Rng().Range(lo, hi); }
    void OnHurt(int hpAfter, int maxHp) override { m_Brain.OnHurt(hpAfter, maxHp); }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_Brain.ShootCd(); }
    glm::vec2 MoveDecision(glm::vec2 chase, float dist) override {
        return m_Brain.ChaseMoveDecision(chase, dist);
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        const int attack = m_Brain.ChooseAttack(); // 1 draw Range(0,100)
        AttackResult r;
        r.fired = true;
        r.pattern = FirePattern::Fan;
        r.count = (attack % 2 == 0) ? BossController::kFanEven : BossController::kFanOdd;
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_Brain.ShootCd();
        return r;
    }

private:
    Game::BossAI01 m_Brain;
};

/// BossAI02 -- ChooseAttack() ungated (always fires); WanderDirection move; no
/// shoot_cd field (uses the controller base cd); OnHurt(hp, max).
class BossAI02Adapter : public BossBrainBase<Game::BossAI02> {
public:
    explicit BossAI02Adapter(float baseCd) : m_BaseCd(baseCd) {}
    void OnHurt(int hpAfter, int maxHp) override { m_Brain.OnHurt(hpAfter, maxHp); }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_BaseCd; }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.WanderDirection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        const int roll = m_Brain.ChooseAttack();
        AttackResult r;
        r.fired = true;
        r.count = BossFanCount(roll);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_BaseCd;
        return r;
    }

private:
    float m_BaseCd;
};

/// BossAI03 -- ChooseAttack() SELF-gated on can_shoot, with NO public way to open
/// the gate (the anim state machine EndAtkNN sets it). The brain is unchanged, so
/// in this pass AI03 is MOVE-ONLY (fires nothing until its owner-side can_shoot
/// cadence is modelled -- recorded debt; AI03 is excluded from the spawn roster).
class BossAI03Adapter : public BossBrainBase<Game::BossAI03> {
public:
    explicit BossAI03Adapter(float baseCd) : m_BaseCd(baseCd) {}
    void OnHurt(int hpAfter, int maxHp) override { m_Brain.OnHurt(hpAfter, maxHp); }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_BaseCd; }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.WanderDirection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        const int atk = m_Brain.ChooseAttack(); // gated -> kNoAttack(0) while can_shoot is false
        AttackResult r;
        r.fired = atk != Game::BossAI03::kNoAttack;
        r.count = BossFanCount(atk);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_BaseCd;
        return r;
    }

private:
    float m_BaseCd;
};

/// BossAI04 -- ChooseAttack() ungated (always fires); STATIONARY (no move method);
/// no shoot_cd field; OnHurt(hp, max).
class BossAI04Adapter : public BossBrainBase<Game::BossAI04> {
public:
    explicit BossAI04Adapter(float baseCd) : m_BaseCd(baseCd) {}
    void OnHurt(int hpAfter, int maxHp) override { m_Brain.OnHurt(hpAfter, maxHp); }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_BaseCd; }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return glm::vec2(0.0F, 0.0F); // BossAI04 has no wander; owner-side positioning.
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        const int atk = m_Brain.ChooseAttack();
        AttackResult r;
        r.fired = true;
        r.count = BossFanCount(atk);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_BaseCd;
        return r;
    }

private:
    float m_BaseCd;
};

/// BossAI05 -- ShootReflection(int&) field-gated (SetCanShoot); WanderDirection
/// move; ShootCd()/SetShootCd; OnHurt(hp,max)->bool (GetHurt gated on awake ->
/// the adapter opens awake so angry can latch).
class BossAI05Adapter : public BossBrainBase<Game::BossAI05> {
public:
    explicit BossAI05Adapter(float baseShootCd) { m_Brain.SetShootCd(baseShootCd); }
    void OnHurt(int hpAfter, int maxHp) override {
        m_Brain.SetAwake(true);
        (void)m_Brain.OnHurt(hpAfter, maxHp);
    }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_Brain.ShootCd(); }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.WanderDirection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        m_Brain.SetCanShoot(true);
        int roll = 0;
        const bool fired = m_Brain.ShootReflection(roll);
        AttackResult r;
        r.fired = fired;
        r.count = BossFanCount(fired ? roll : 0);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_Brain.ShootCd();
        return r;
    }
};

/// BossAI06 -- ShootReflectionRoll(int&) field-gated (SetCanShoot); WanderDirection
/// move; no shoot_cd field; OnHurt(hp,max,awake). SPECIAL: the child-ball summon
/// (BossAI06Child) is NOT modelled here (deferred summon subsystem); the boss
/// still fires its own bullets.
class BossAI06Adapter : public BossBrainBase<Game::BossAI06> {
public:
    explicit BossAI06Adapter(float baseCd) : m_BaseCd(baseCd) {}
    void OnHurt(int hpAfter, int maxHp) override {
        m_Brain.OnHurt(hpAfter, maxHp, /*awake=*/true);
    }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_BaseCd; }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.WanderDirection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        m_Brain.SetCanShoot(true);
        int roll = 0;
        const bool fired = m_Brain.ShootReflectionRoll(roll);
        AttackResult r;
        r.fired = fired;
        r.count = BossFanCount(fired ? roll : 0);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_BaseCd;
        return r;
    }

private:
    float m_BaseCd;
};

/// BossAI07 -- ShootReflection(float&): with can_shoot it dispatches an attack and
/// returns true (NO roll). STATIONARY (no move method); ShootCd()/SetShootCd;
/// BossAngry() has no hp gate, so the adapter applies the hp/max<0.5 enrage itself.
class BossAI07Adapter : public BossBrainBase<Game::BossAI07> {
public:
    explicit BossAI07Adapter(float baseShootCd) { m_Brain.SetShootCd(baseShootCd); }
    void OnHurt(int hpAfter, int maxHp) override {
        // BossAI07.BossAngry has no hp gate (it halves shoot_cd every call); apply
        // the standard hp/max < 0.5 once-only enrage here.
        if (maxHp > 0 &&
            static_cast<float>(hpAfter) / static_cast<float>(maxHp) < 0.5F &&
            !m_Brain.Angry()) {
            m_Brain.BossAngry();
        }
    }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_Brain.ShootCd(); }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return glm::vec2(0.0F, 0.0F);
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        m_Brain.SetCanShoot(true);
        float reInvokeDelay = 0.0F;
        const bool fired = m_Brain.ShootReflection(reInvokeDelay); // true -> dispatched attack
        AttackResult r;
        r.fired = fired;
        r.count = BossController::kFanEven; // AI07 surfaces no attack roll here.
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_Brain.ShootCd();
        return r;
    }
};

/// BossAI08 -- ChooseAttack(canShoot,dead,dizzy) param-gated; WanderDirection move;
/// ShootCd()/SetShootCd; OnHurt(awake,dead,hp,max).
class BossAI08Adapter : public BossBrainBase<Game::BossAI08> {
public:
    explicit BossAI08Adapter(float baseShootCd) { m_Brain.SetShootCd(baseShootCd); }
    void OnHurt(int hpAfter, int maxHp) override {
        m_Brain.OnHurt(/*awake=*/true, /*dead=*/false, hpAfter, maxHp);
    }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_Brain.ShootCd(); }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.WanderDirection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        const int roll =
            m_Brain.ChooseAttack(/*canShoot=*/true, /*dead=*/false, /*dizzy=*/false);
        AttackResult r;
        r.fired = roll >= 0; // gate open -> roll in [0,100); -1 only when gated.
        r.count = BossFanCount(roll);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_Brain.ShootCd();
        return r;
    }
};

/// BossAI09 -- ShootReflection() int field-gated (SetCanShoot); RunReflection move;
/// ShootCd()/SetShootCd; OnHurt(hp,max).
class BossAI09Adapter : public BossBrainBase<Game::BossAI09> {
public:
    explicit BossAI09Adapter(float baseShootCd) { m_Brain.SetShootCd(baseShootCd); }
    void OnHurt(int hpAfter, int maxHp) override { m_Brain.OnHurt(hpAfter, maxHp); }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_Brain.ShootCd(); }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.RunReflection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        m_Brain.SetCanShoot(true);
        const int atk = m_Brain.ShootReflection(); // [1,5] gate-open, kNoAttack(0) gated
        AttackResult r;
        r.fired = atk != Game::BossAI09::kNoAttack;
        r.count = BossFanCount(atk);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_Brain.ShootCd();
        return r;
    }
};

/// BossAI10 -- ShootReflectionChooseAttack() field-gated (SetCanShoot);
/// RunReflectionDirection move; no ShootCd getter (OnHurt returns the cd); the
/// adapter tracks the cadence in m_ShootCd. OnHurt gated on awake (SetAwake).
class BossAI10Adapter : public BossBrainBase<Game::BossAI10> {
public:
    explicit BossAI10Adapter(float baseShootCd) : m_ShootCd(baseShootCd) {}
    void OnHurt(int hpAfter, int maxHp) override {
        m_Brain.SetAwake(true);
        m_ShootCd = m_Brain.OnHurt(hpAfter, maxHp, m_ShootCd);
    }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_ShootCd; }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.RunReflectionDirection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        m_Brain.SetCanShoot(true);
        const int atk = m_Brain.ShootReflectionChooseAttack(); // [1,5] / kNoAttack(0)
        AttackResult r;
        r.fired = atk != Game::BossAI10::kNoAttack;
        r.count = BossFanCount(atk);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_ShootCd;
        return r;
    }

private:
    float m_ShootCd;
};

/// BossAI11 -- ShootReflection() int field-gated (SetCanShoot); WanderDirection
/// move; ShootCd()/SetShootCd; OnHurt(hp,max) (GetHurt gated on awake -> SetAwake).
class BossAI11Adapter : public BossBrainBase<Game::BossAI11> {
public:
    explicit BossAI11Adapter(float baseShootCd) { m_Brain.SetShootCd(baseShootCd); }
    void OnHurt(int hpAfter, int maxHp) override {
        m_Brain.SetAwake(true);
        m_Brain.OnHurt(hpAfter, maxHp);
    }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_Brain.ShootCd(); }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.WanderDirection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        m_Brain.SetCanShoot(true);
        const int roll = m_Brain.ShootReflection(); // [0,100) gate-open, -1 gated
        AttackResult r;
        r.fired = roll >= 0;
        r.count = BossFanCount(roll);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_Brain.ShootCd();
        return r;
    }
};

/// BossAI12 -- ShootReflectionRoll(canShoot,dead,dizzy,int&) param-gated;
/// WanderDirection move; ShootCd()/SetShootCd; OnHurt(awake,hp,max). SPECIAL: the
/// two-half composite HP (BossAI12Parent) is NOT modelled here (single-entity HP,
/// deferred composite-HP subsystem); the boss still fires its own bullets.
class BossAI12Adapter : public BossBrainBase<Game::BossAI12> {
public:
    explicit BossAI12Adapter(float baseShootCd) { m_Brain.SetShootCd(baseShootCd); }
    void OnHurt(int hpAfter, int maxHp) override {
        m_Brain.OnHurt(/*awake=*/true, hpAfter, maxHp);
    }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_Brain.ShootCd(); }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.WanderDirection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        int roll = 0;
        const bool fired = m_Brain.ShootReflectionRoll(/*canShoot=*/true, /*dead=*/false,
                                                       /*dizzy=*/false, roll);
        AttackResult r;
        r.fired = fired;
        r.count = BossFanCount(fired ? roll : 0);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_Brain.ShootCd();
        return r;
    }
};

/// BossAI13 -- ShootReflection(int&) field-gated (SetCanShoot); RunReflection move;
/// ShootCd()/SetShootCd; OnHurt(hp,max) (GetHurt gated on awake; no SetAwake, so
/// the adapter wakes the brain once via OnGameStateChange in its ctor).
class BossAI13Adapter : public BossBrainBase<Game::BossAI13> {
public:
    explicit BossAI13Adapter(float baseShootCd) {
        m_Brain.SetShootCd(baseShootCd);
        (void)m_Brain.OnGameStateChange(1, /*roomReady=*/true); // wake (no RNG) so OnHurt latches.
    }
    void OnHurt(int hpAfter, int maxHp) override { m_Brain.OnHurt(hpAfter, maxHp); }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_Brain.ShootCd(); }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return m_Brain.RunReflection();
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        m_Brain.SetCanShoot(true);
        int roll = 0;
        const bool fired = m_Brain.ShootReflection(roll);
        AttackResult r;
        r.fired = fired;
        r.count = BossFanCount(fired ? roll : 0);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_Brain.ShootCd();
        return r;
    }
};

/// BossAI14 -- ShootReflectionChoose() field-gated (SetCanShoot); STATIONARY in
/// this pass (rooted octopus -- skips the root/heading move branching); no ShootCd
/// getter (BossAngry returns the cd), tracked in m_ShootCd; ctor runs Construct()
/// (seeds root/can_hit). BossAngry has no hp gate -> the adapter applies it.
class BossAI14Adapter : public BossBrainBase<Game::BossAI14> {
public:
    explicit BossAI14Adapter(float baseShootCd) : m_ShootCd(baseShootCd) {
        m_Brain.Construct();
    }
    void OnHurt(int hpAfter, int maxHp) override {
        if (maxHp > 0 &&
            static_cast<float>(hpAfter) / static_cast<float>(maxHp) < 0.5F &&
            !m_Brain.Angry()) {
            m_ShootCd = m_Brain.BossAngry(m_ShootCd);
        }
    }
    bool Angry() const override { return m_Brain.Angry(); }
    float ShootCd() const override { return m_ShootCd; }
    glm::vec2 MoveDecision(glm::vec2 /*chase*/, float /*dist*/) override {
        return glm::vec2(0.0F, 0.0F);
    }
    AttackResult AttackTick(float /*baseCd*/) override {
        m_Brain.SetCanShoot(true);
        const int atk = m_Brain.ShootReflectionChoose(); // [1,6] / kNoAttack(0)
        AttackResult r;
        r.fired = atk != Game::BossAI14::kNoAttack;
        r.count = BossFanCount(atk);
        r.spreadDeg = BossController::kFanSpreadDeg;
        r.nextCd = m_ShootCd;
        return r;
    }

private:
    float m_ShootCd;
};

/// Dispatch a boss id -> its brain adapter (the (d) registry, via BrainFactory).
/// All 14 standard fighting bosses registered. Unknown id falls back to BossAI01
/// (safe default + back-compat). @p baseShootCd is the controller base cadence
/// (BossAI01 needs it at construction; the rest seed it into their cadence field).
inline std::unique_ptr<IBossBrain> MakeBossBrain(const std::string &bossId,
                                                 float baseShootCd) {
    if (bossId == "BossAI02") {
        return std::make_unique<BossAI02Adapter>(baseShootCd);
    }
    if (bossId == "BossAI03") {
        return std::make_unique<BossAI03Adapter>(baseShootCd);
    }
    if (bossId == "BossAI04") {
        return std::make_unique<BossAI04Adapter>(baseShootCd);
    }
    if (bossId == "BossAI05") {
        return std::make_unique<BossAI05Adapter>(baseShootCd);
    }
    if (bossId == "BossAI06") {
        return std::make_unique<BossAI06Adapter>(baseShootCd);
    }
    if (bossId == "BossAI07") {
        return std::make_unique<BossAI07Adapter>(baseShootCd);
    }
    if (bossId == "BossAI08") {
        return std::make_unique<BossAI08Adapter>(baseShootCd);
    }
    if (bossId == "BossAI09") {
        return std::make_unique<BossAI09Adapter>(baseShootCd);
    }
    if (bossId == "BossAI10") {
        return std::make_unique<BossAI10Adapter>(baseShootCd);
    }
    if (bossId == "BossAI11") {
        return std::make_unique<BossAI11Adapter>(baseShootCd);
    }
    if (bossId == "BossAI12") {
        return std::make_unique<BossAI12Adapter>(baseShootCd);
    }
    if (bossId == "BossAI13") {
        return std::make_unique<BossAI13Adapter>(baseShootCd);
    }
    if (bossId == "BossAI14") {
        return std::make_unique<BossAI14Adapter>(baseShootCd);
    }
    return std::make_unique<BossAI01Adapter>(baseShootCd); // BossAI01 + default fallback
}

} // namespace Game::Sim

#endif /* GAME_SIM_BOSSBRAINADAPTERS_HPP */
