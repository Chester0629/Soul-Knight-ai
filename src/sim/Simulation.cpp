#include "sim/Simulation.hpp"

#include <algorithm>

#include "combat/Damage.hpp"
#include "sim/BrainFactory.hpp"
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
void Simulation::TickWeapon() {
    if (!m_Weapon.has_value()) {
        return;
    }
    const std::size_t before = m_FireIntents.size();
    m_Weapon->Tick(m_Input.firing, m_Input.playerPos, m_Input.aimDir, m_FireIntents);
    m_PlayerShotsLastAdvance += static_cast<int>(m_FireIntents.size() - before);
}
void Simulation::MoveControllers() {
    for (auto &e : m_Enemies) {
        if (e->State().dead) {
            continue;
        }
        const glm::vec2 vel = e->ComputeVelocity(); // ONCE per step (decays knockback).
        glm::vec2 pos = e->State().pos;
        // Axis-separated wall slide (same shape as the GameScene player pattern).
        const glm::vec2 tryX{pos.x + vel.x * kFixedStepSeconds, pos.y};
        if (!m_World->Blocks(tryX, kEnemyBodyRadius)) {
            pos.x = tryX.x;
        }
        const glm::vec2 tryY{pos.x, pos.y + vel.y * kFixedStepSeconds};
        if (!m_World->Blocks(tryY, kEnemyBodyRadius)) {
            pos.y = tryY.y;
        }
        e->MutableState().pos = pos;
        const glm::vec2 md = e->MoveDir(); // steering heading (excludes knockback shove).
        if (md.x != 0.0F || md.y != 0.0F) {
            e->MutableState().facing = Normalize(md);
        }
    }
    if (m_Boss != nullptr && !m_Boss->State().dead && m_Boss->State().awake) {
        const glm::vec2 dir = m_Boss->ChaseDir();
        const glm::vec2 vel = dir * BossController::kSpeed;
        glm::vec2 pos = m_Boss->State().pos;
        const glm::vec2 tryX{pos.x + vel.x * kFixedStepSeconds, pos.y};
        if (!m_World->Blocks(tryX, kBossBodyRadius)) {
            pos.x = tryX.x;
        }
        const glm::vec2 tryY{pos.x, pos.y + vel.y * kFixedStepSeconds};
        if (!m_World->Blocks(tryY, kBossBodyRadius)) {
            pos.y = tryY.y;
        }
        m_Boss->MutableState().pos = pos;
        m_Boss->MutableState().facing = dir; // the boss faces the player it chases.
    }
}
void Simulation::DrainFireIntents() {
    for (const FireIntent &fi : m_FireIntents) {
        m_FireSystem.Expand(fi, m_NextBulletId, m_Bullets);
    }
    m_FireIntents.clear();
}
void Simulation::IntegrateBullets() {
    for (BulletState &b : m_Bullets) {
        if (!b.active) {
            continue;
        }
        b.pos += b.vel * kFixedStepSeconds;
        b.lifeMs -= kFixedStepMs;
        if (!b.canThrough && m_World->Blocks(b.pos, kBulletRadius)) {
            b.active = false;
        }
    }
}
void Simulation::ResolveHits() {
    // Deterministic order: ascending bullet id (m_Bullets is append-ordered by id, but
    // sort defensively so a future reordering cannot desync the hit RNG stream).
    std::sort(m_Bullets.begin(), m_Bullets.end(),
              [](const BulletState &a, const BulletState &b) { return a.id < b.id; });

    for (BulletState &b : m_Bullets) {
        if (!b.active) {
            continue;
        }
        if (b.camp == 0) {
            // Player bullet -> enemies, then boss. One target per bullet per step.
            // The enemy gate tests the `dead` FLAG (set later in Cull), not stats.IsDead():
            // a second bullet landing the same step as a lethal one is consumed on the
            // about-to-die enemy. Intentional + deterministic -- all in-flight bullets land.
            bool consumed = false;
            for (auto &e : m_Enemies) {
                if (e->State().dead || !CirclesOverlap(b.pos, kBulletRadius, e->State().pos,
                                                       kEnemyBodyRadius)) {
                    continue;
                }
                Game::Combat::AttackerInput in;
                in.baseDamage = b.damage;
                in.critical = b.critical;
                in.repelInputMagnitude = b.repel * kRepelScale;
                const Game::Combat::HitResult hr =
                    Game::Combat::ResolveHit(in, m_HitRng, Game::Combat::Defender::ENEMY);
                Game::Combat::ApplyToEnemy(e->MutableState().stats, hr, true);
                if (!e->State().kinematic) {
                    e->ApplyForce(Normalize(b.vel), hr.repelMagnitude);
                }
                consumed = true;
                break;
            }
            if (!consumed && m_Boss != nullptr && !m_Boss->State().dead &&
                CirclesOverlap(b.pos, kBulletRadius, m_Boss->State().pos, kBossBodyRadius)) {
                Game::Combat::AttackerInput in;
                in.baseDamage = b.damage;
                in.critical = b.critical;
                in.repelInputMagnitude = b.repel * kRepelScale;
                const Game::Combat::HitResult hr =
                    Game::Combat::ResolveHit(in, m_HitRng, Game::Combat::Defender::ENEMY);
                Game::Combat::ApplyToEnemy(m_Boss->MutableState().stats, hr, true);
                m_Boss->OnHurt(m_Boss->State().stats.hp, m_Boss->State().stats.maxHp);
                consumed = true;
            }
            if (consumed && b.pierce <= 0) {
                b.active = false;
            } else if (consumed) {
                --b.pierce; // canThrough/pierce: survive and keep going.
            }
        } else {
            // Enemy bullet -> player.
            if (m_Input.playerAlive &&
                CirclesOverlap(b.pos, kBulletRadius, m_Input.playerPos, kPlayerBodyRadius)) {
                Game::Combat::AttackerInput in;
                in.baseDamage = b.damage;
                in.critical = b.critical;
                in.repelInputMagnitude = b.repel * kRepelScale;
                const Game::Combat::HitResult hr =
                    Game::Combat::ResolveHit(in, m_HitRng, Game::Combat::Defender::PLAYER);
                Game::Combat::ApplyToPlayer(m_PlayerStats, hr, true);
                b.active = false;
            }
        }
    }
}
void Simulation::Cull() {
    for (auto &e : m_Enemies) {
        if (!e->State().dead && e->State().stats.IsDead()) {
            e->Kill(); // cancels its scheduler cadence; kept in m_Enemies for stable view id.
        }
    }
    if (m_Boss != nullptr && !m_Boss->State().dead && m_Boss->State().stats.IsDead()) {
        m_Boss->Kill();
    }
    m_Bullets.erase(std::remove_if(m_Bullets.begin(), m_Bullets.end(),
                                   [](const BulletState &b) {
                                       return !b.active || b.lifeMs <= 0.0F;
                                   }),
                    m_Bullets.end());
}

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
void Simulation::AddEnemy(const Game::EnemyDef &def, glm::vec2 spawn, int roomId, int seed) {
    std::unique_ptr<EnemyController> ec = BrainFactory::MakeEnemyPtr(def, spawn, seed);
    ec->MutableState().roomId = roomId;
    ec->MutableState().stats.hp = kSliceEnemyHp;    // EnemyDef has no hp; seed the slice value.
    ec->MutableState().stats.maxHp = kSliceEnemyHp; // (EnemyController leaves stats.hp == 0).
    ec->Activate(m_Scheduler, m_FireIntents);        // asleep until WakeByRoom().
    m_Enemies.push_back(std::move(ec));
}
void Simulation::SetBoss(float baseShootCd, glm::vec2 spawn, int maxHp, int roomId, int seed) {
    m_Boss = std::make_unique<BossController>(baseShootCd, spawn, maxHp, seed);
    m_Boss->MutableState().roomId = roomId;
    m_Boss->Activate(m_Scheduler, m_FireIntents); // asleep until WakeByRoom().
}
void Simulation::EquipWeapon(const Game::WeaponDef &def, const std::string &weaponId,
                             int seed) {
    m_Weapon.emplace(BrainFactory::MakeWeapon(def, weaponId, seed)); // cold rebuild
}

} // namespace Game::Sim
