#ifndef GAME_SIM_ENEMYBRAINADAPTERS_HPP
#define GAME_SIM_ENEMYBRAINADAPTERS_HPP

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "combat/EnemyAI01.hpp"
#include "combat/EnemyAI02.hpp"
#include "combat/EnemyAI03.hpp"
#include "combat/EnemyAI04.hpp"
#include "combat/EnemyAI06.hpp"
#include "combat/EnemyAI07.hpp"
#include "combat/EnemyAI08.hpp"
#include "combat/EnemyAI09.hpp"
#include "combat/EnemyAI10.hpp"
#include "combat/EnemyAI11.hpp"
#include "combat/EnemyAI12.hpp"
#include "combat/EnemyAI13.hpp"
#include "combat/EnemyAI14.hpp"
#include "combat/EnemyAI15.hpp"
#include "combat/EnemyAIShark.hpp"
#include "sim/IEnemyBrain.hpp"

namespace Game::Sim {

// === (d) per-brain adapters. The brain SOURCE is NEVER modified -- each adapter
// only CALLS public brain methods and translates that brain's heterogeneous tick
// signatures into IEnemyBrain. All 15 ported brains (EnemyAI01-04/06-15 + Shark;
// there is no EnemyAI05 brain) are registered in MakeEnemyBrain below. ============

// --- Shared templates for the common signature shapes --------------------------

/// bool ShootReflection(float& out, float base) where `out` is the next-fire
/// delay/cd. Covers AI09/10/11/12 (out = shootCd / onAtkDelay / fireBallDelay).
template <class T>
class CdShootAdapter : public EnemyBrainBase<T> {
public:
    void Scout() override { this->m_Brain.Scout(); }
    glm::vec2 RunReflection() override { return this->m_Brain.RunReflection(); }
    IEnemyBrain::ShootResult ShootTick(float baseCd) override {
        float out = baseCd;
        const bool fired = this->m_Brain.ShootReflection(out, baseCd);
        return {fired, out};
    }
};

/// int ShootReflection() -> fired iff a non-zero attack was selected. The brain
/// carries no out-cd, so the controller's base cd reschedules. Covers AI07/15/Shark.
template <class T>
class IntShootAdapter : public EnemyBrainBase<T> {
public:
    void Scout() override { this->m_Brain.Scout(); }
    glm::vec2 RunReflection() override { return this->m_Brain.RunReflection(); }
    IEnemyBrain::ShootResult ShootTick(float baseCd) override {
        return {this->m_Brain.ShootReflection() > 0, baseCd};
    }
};

/// No ShootReflection -> MELEE/contact brain (move only). The brain drives the
/// chase (Scout/RunReflection); the actual contact/boom damage needs a sim
/// contact-damage subsystem that is DEFERRED (owner-side amount/timing -- see
/// docs/LEVEL_GEN_PLAN.md s7). So ShootTick never fires. Covers AI04/08.
template <class T>
class MoveOnlyAdapter : public EnemyBrainBase<T> {
public:
    void Scout() override { this->m_Brain.Scout(); }
    glm::vec2 RunReflection() override { return this->m_Brain.RunReflection(); }
    IEnemyBrain::ShootResult ShootTick(float baseCd) override {
        return {false, baseCd};
    }
};

// --- Explicit adapters for the outlier signatures ------------------------------

/// EnemyAI01 -- standard. Forwards VERBATIM so the AI01 path is byte-identical to
/// the pre-(d) controller (the determinism gate). Sole brain with SetKinematic.
class EnemyAI01Adapter : public EnemyBrainBase<Game::EnemyAI01> {
public:
    void SetKinematic(bool v) override { m_Brain.SetKinematic(v); }
    void Scout() override { m_Brain.Scout(); }
    glm::vec2 RunReflection() override { return m_Brain.RunReflection(); }
    ShootResult ShootTick(float baseCd) override {
        float cd = baseCd;
        const bool fired = m_Brain.ShootReflection(cd, baseCd);
        return {fired, cd};
    }
};

/// EnemyAI02 -- ShootReflection(float& speedRate): the out is a speed rate, NOT a
/// cd, so it is consumed (sets the brain's own state) but the slice keeps base cd.
class EnemyAI02Adapter : public EnemyBrainBase<Game::EnemyAI02> {
public:
    void Scout() override { m_Brain.Scout(); }
    glm::vec2 RunReflection() override { return m_Brain.RunReflection(); }
    ShootResult ShootTick(float baseCd) override {
        float speedRate = 0.0F;
        const bool fired = m_Brain.ShootReflection(speedRate);
        return {fired, baseCd};
    }
};

/// EnemyAI03 -- stand-and-shoot with shooting STATE (CanShoot) but NO
/// ShootReflection decision method. Reconstruct the fire at the adapter layer:
/// fire on the controller's shoot cadence whenever the brain reports CanShoot
/// (the controller already awake-gates this tick). brain UNCHANGED. (Faithful
/// gating via AI03's full Shooting/StandIn state machine = refinement debt, s7.)
class EnemyAI03Adapter : public EnemyBrainBase<Game::EnemyAI03> {
public:
    void Scout() override { m_Brain.Scout(); }
    glm::vec2 RunReflection() override { return m_Brain.RunReflection(); }
    ShootResult ShootTick(float baseCd) override {
        return {m_Brain.CanShoot(), baseCd};
    }
};

/// EnemyAI06 -- fixed TURRET: no RunReflection (never wanders);
/// ShootReflection(int& outRoll) returns fired + a re-fire ceiling roll (not a cd).
class EnemyAI06Adapter : public EnemyBrainBase<Game::EnemyAI06> {
public:
    void Scout() override { m_Brain.Scout(); }
    glm::vec2 RunReflection() override { return glm::vec2(0.0F, 0.0F); }
    ShootResult ShootTick(float baseCd) override {
        int roll = 0;
        const bool fired = m_Brain.ShootReflection(roll);
        return {fired, baseCd};
    }
};

/// EnemyAI13 -- child + boom (CanHit/BoomLight). Scout is void; RunReflection takes
/// an out-dir (gated on can_hit -> stays 0 when it cannot move). Melee/contact:
/// ShootTick never fires (contact-damage subsystem deferred, s7).
class EnemyAI13Adapter : public EnemyBrainBase<Game::EnemyAI13> {
public:
    void Scout() override { m_Brain.Scout(); }
    glm::vec2 RunReflection() override {
        glm::vec2 dir(0.0F, 0.0F);
        m_Brain.RunReflection(dir);
        return dir;
    }
    ShootResult ShootTick(float baseCd) override { return {false, baseCd}; }
};

/// EnemyAI14 -- ShootReflection(float& atkDelay, float& endDelay, float base): two
/// out delays; use the attack delay as the next-fire cadence.
class EnemyAI14Adapter : public EnemyBrainBase<Game::EnemyAI14> {
public:
    void Scout() override { m_Brain.Scout(); }
    glm::vec2 RunReflection() override { return m_Brain.RunReflection(); }
    ShootResult ShootTick(float baseCd) override {
        float atkDelay = baseCd;
        float endDelay = 0.0F;
        const bool fired = m_Brain.ShootReflection(atkDelay, endDelay, baseCd);
        return {fired, atkDelay};
    }
};

/// Dispatch an enemy id -> its brain adapter (the (d) registry, via BrainFactory).
/// All 15 brains registered. Unknown id (incl. "EnemyAI05", which has no brain)
/// falls back to AI01 (safe default + back-compat).
inline std::unique_ptr<IEnemyBrain> MakeEnemyBrain(const std::string &enemyId) {
    if (enemyId == "EnemyAI02") {
        return std::make_unique<EnemyAI02Adapter>();
    }
    if (enemyId == "EnemyAI03") {
        return std::make_unique<EnemyAI03Adapter>();
    }
    if (enemyId == "EnemyAI04") {
        return std::make_unique<MoveOnlyAdapter<Game::EnemyAI04>>();
    }
    if (enemyId == "EnemyAI06") {
        return std::make_unique<EnemyAI06Adapter>();
    }
    if (enemyId == "EnemyAI07") {
        return std::make_unique<IntShootAdapter<Game::EnemyAI07>>();
    }
    if (enemyId == "EnemyAI08") {
        return std::make_unique<MoveOnlyAdapter<Game::EnemyAI08>>();
    }
    if (enemyId == "EnemyAI09") {
        return std::make_unique<CdShootAdapter<Game::EnemyAI09>>();
    }
    if (enemyId == "EnemyAI10") {
        return std::make_unique<CdShootAdapter<Game::EnemyAI10>>();
    }
    if (enemyId == "EnemyAI11") {
        return std::make_unique<CdShootAdapter<Game::EnemyAI11>>();
    }
    if (enemyId == "EnemyAI12") {
        return std::make_unique<CdShootAdapter<Game::EnemyAI12>>();
    }
    if (enemyId == "EnemyAI13") {
        return std::make_unique<EnemyAI13Adapter>();
    }
    if (enemyId == "EnemyAI14") {
        return std::make_unique<EnemyAI14Adapter>();
    }
    if (enemyId == "EnemyAI15") {
        return std::make_unique<IntShootAdapter<Game::EnemyAI15>>();
    }
    if (enemyId == "EnemyAIShark") {
        return std::make_unique<IntShootAdapter<Game::EnemyAIShark>>();
    }
    return std::make_unique<EnemyAI01Adapter>(); // EnemyAI01 + default fallback
}

} // namespace Game::Sim

#endif /* GAME_SIM_ENEMYBRAINADAPTERS_HPP */
