#include "sim/Simulation.hpp"

#include <algorithm>

#include "sim/SimConfig.hpp"
#include "sim/SimMath.hpp"

namespace Game::Sim {

Simulation::Simulation(int runSeed, WorldCollision *world)
    : m_RunSeed(runSeed), m_World(world != nullptr ? world : &m_NullWorld) {
    m_HitRng.SetRandomSeed(runSeed ^ kHitRngSalt);
    // The controllers capture &m_FireIntents; reserve so the buffer is stable churn-free.
    m_FireIntents.reserve(64);
}

void Simulation::SetPlayerStats(const Game::CombatStats &stats) { m_PlayerStats = stats; }

std::vector<SimEvent> Simulation::DrainEvents() {
    std::vector<SimEvent> out;
    out.swap(m_Events);
    return out;
}

Simulation::EntityView Simulation::ViewOf(const EntityState &s, std::uint32_t id) {
    EntityView v;
    v.id = id;
    v.pos = s.pos;
    v.facing = s.facing;
    v.hp = s.stats.hp;
    v.maxHp = s.stats.maxHp;
    v.alive = !s.dead;
    v.roomId = s.roomId;
    return v;
}

std::vector<Simulation::EntityView> Simulation::EnemyViews() const {
    std::vector<EntityView> views;
    views.reserve(m_Enemies.size());
    std::uint32_t id = 0;
    for (const auto &e : m_Enemies) {
        views.push_back(ViewOf(e->State(), id));
        ++id;
    }
    return views;
}

Simulation::EntityView Simulation::BossView() const {
    return m_Boss != nullptr ? ViewOf(m_Boss->State(), kBossViewId) : EntityView{};
}

void Simulation::WakeByRoom() {
    for (auto &e : m_Enemies) {
        e->MutableState().awake = (e->State().roomId == m_Input.playerRoomId);
    }
    if (m_Boss != nullptr) {
        m_Boss->MutableState().awake = (m_Boss->State().roomId == m_Input.playerRoomId);
    }
}

// Per-step helpers filled across Tasks 3-7.
void Simulation::TickWeapon() {}
void Simulation::MoveControllers() {}
void Simulation::DrainFireIntents() {}
void Simulation::IntegrateBullets() {}
void Simulation::ResolveHits() {}
void Simulation::Cull() {}

void Simulation::Step() {
    const glm::vec2 target = m_Input.playerPos;
    for (auto &e : m_Enemies) {
        e->SetTarget(target);
    }
    if (m_Boss != nullptr) {
        m_Boss->SetTarget(target);
    }
    m_Scheduler.Tick();
    TickWeapon();
    MoveControllers();
    DrainFireIntents();
    IntegrateBullets();
    ResolveHits();
    Cull();
}

int Simulation::Advance(float dtMs, const WorldInputs &in) {
    m_Input = in;
    m_PlayerShotsLastAdvance = 0;
    WakeByRoom();
    const int steps = m_Clock.Advance(dtMs);
    for (int i = 0; i < steps; ++i) {
        Step();
    }
    return steps;
}

// AddEnemy / SetBoss / EquipWeapon implemented in Tasks 3, 5, 6.
void Simulation::AddEnemy(const Game::EnemyDef &, glm::vec2, int, int) {}
void Simulation::SetBoss(float, glm::vec2, int, int, int) {}
void Simulation::EquipWeapon(const Game::WeaponDef &, const std::string &, int) {}

} // namespace Game::Sim
