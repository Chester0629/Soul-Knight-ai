#include "sim/Simulation.hpp"

#include <algorithm>

#include "combat/Damage.hpp"
#include "sim/BrainFactory.hpp"
#include "sim/CharSkillAdapters.hpp"
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
    const int pulls =
        m_Weapon->Tick(m_Input.firing, m_Input.playerPos, m_Input.aimDir, m_FireIntents);
    const std::size_t shots = m_FireIntents.size() - before; // intents (pellets) added this step.
    // B1-P3: energy + the muzzle cue are PER-PULL, not per-intent -- a Fan pull is one
    // pull (one energy, one flash) regardless of how many pellets it spawned.
    m_PlayerShotsLastAdvance += pulls; // primary hand only -> energy spend.
    for (int i = 0; i < pulls; ++i) {  // A: one "fire" cue per pull (muzzle flash).
        m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kPlayerViewId, "fire"});
    }

    // --- C01 skill effect (to the degree the sim supports; honest fidelity flag) ---
    // While the ultimate is active, the second hand MIRRORS the primary attack
    // (CharSkillC01::RoleAtk -> mirrorSecondHand). Modeled as a FREE duplicate of this
    // step's primary shots, offset perpendicular to aim as a stand-in for the second
    // hand's position. NOT FAITHFUL-COMPLETE: the real c01 second hand is a SEPARATE
    // gun with its own cadence, fired via a 0.1s-delayed Invoke ("Hand2Atk"), with the
    // actual hand transform + animation -- all owner/presentation, the truncated
    // get_transform tail-call the brain does not model. Full c01 fidelity = B1b.
    if (m_Skill != nullptr && shots > 0) {
        const ICharSkill::AtkEffect d =
            m_Skill->RoleAtk(/*pressDown=*/m_Input.firing, /*standingOnItem=*/false);
        if (d.mirrorSecondHand && d.secondHandValue) {
            constexpr float kSecondHandOffsetPx = 12.0F; // placeholder hand offset (B1b: real transform).
            // Copy the primary shots first: push_back below may reallocate the buffer.
            const std::vector<FireIntent> primary(
                m_FireIntents.begin() + static_cast<std::ptrdiff_t>(before),
                m_FireIntents.begin() + static_cast<std::ptrdiff_t>(before + shots));
            for (FireIntent mirror : primary) {
                const glm::vec2 perp{-mirror.dir.y, mirror.dir.x};
                mirror.origin += perp * kSecondHandOffsetPx;
                m_FireIntents.push_back(mirror);
                m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kPlayerViewId, "fire"});
            }
            // The mirror is a FREE bonus: intentionally NOT added to
            // m_PlayerShotsLastAdvance, so the second hand does not double energy spend.
        }
    }
}

void Simulation::TickSkill() {
    if (m_Skill == nullptr) {
        return;
    }
    // Activation (edge trigger): the skill button this step tries to enter the
    // ultimate via the (d) adapter. Idempotent -- the brain gates on cooldown/
    // in_skill, so a held / multi-step-constant input cannot re-cast.
    if (m_Input.skill) {
        const ICharSkill::TriggerResult r = m_Skill->TryTrigger();
        if (r.dashImpulse) {
            // C02: the ultimate is a forward dash. The sim surfaces the impulse as an
            // event the owner (GameScene/driver) applies to player movement -- the
            // sim's player position is an INPUT, not sim-owned. Effect-bearing skill
            // output without perturbing the bullet/entity replay traces.
            m_Events.push_back(
                SimEvent{SimEventType::AnimTrigger, kPlayerViewId, "skill_dash"});
        }
    }
    // Per-frame Update: cooldown counts UP (NOT frozen while in_skill) + (for windowed
    // heroes) the active window counts down and auto-ends (ICharSkill::Tick).
    m_Skill->Tick(kFixedStepMs);
    // A3 cooldown bridge: publish the 0..1 charge into the player stats the shell
    // pulls back each frame (-> CombatStats::skillCdProgress -> HUD; A4 renders it).
    m_PlayerStats.skillCdProgress = m_Skill->CooldownProgress();
}

// A: turn each controller's latched shot into an "attack" AnimTrigger keyed by its view id
// (enemy = m_Enemies index, boss = kBossViewId). Drained once per step after the scheduler
// has run the OnShootTick callbacks. Purely presentation -- no RNG, no effect on the sim.
void Simulation::EmitAttackEvents() {
    std::uint32_t id = 0;
    for (auto &e : m_Enemies) {
        if (e->ConsumeFiredThisStep()) {
            m_Events.push_back(SimEvent{SimEventType::AnimTrigger, id, "attack"});
        }
        ++id;
    }
    if (m_Boss != nullptr && m_Boss->ConsumeFiredThisStep()) {
        m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kBossViewId, "attack"});
    }
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
        const glm::vec2 chase = m_Boss->ChaseDir(); // toward player: facing + the decision base.
        glm::vec2 moveDir = m_Boss->MoveDir();      // F2: RunReflection move decision (chase/strafe/retreat).
        if (moveDir.x == 0.0F && moveDir.y == 0.0F) {
            moveDir = chase; // before the first think tick: fall back to chase (no idle freeze).
        }
        const glm::vec2 vel = moveDir * BossController::kSpeed;
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
        m_Boss->MutableState().facing = chase; // the boss faces the player it chases.
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
            // Phase 3: a consumed bullet damages a destructible design-room box at
            // the impact point (no-op for walls / under NullWorldCollision, so the
            // combat hash is unchanged). Scene-side mutation; sim RNG untouched.
            m_World->DamageObstacle(b.pos, kBulletRadius);
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
            for (std::size_t ei = 0; ei < m_Enemies.size(); ++ei) {
                auto &e = m_Enemies[ei];
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
                m_Events.push_back(SimEvent{SimEventType::AnimTrigger,
                                            static_cast<std::uint32_t>(ei), "hurt"}); // A
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
                m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kBossViewId, "hurt"}); // A
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
                m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kPlayerViewId, "hurt"}); // A
                b.active = false;
            }
        }
    }
}
void Simulation::Cull() {
    for (std::size_t ei = 0; ei < m_Enemies.size(); ++ei) {
        auto &e = m_Enemies[ei];
        if (!e->State().dead && e->State().stats.IsDead()) {
            e->Kill(); // cancels its scheduler cadence; kept in m_Enemies for stable view id.
            m_Events.push_back(SimEvent{SimEventType::AnimTrigger,
                                        static_cast<std::uint32_t>(ei), "death"}); // A
        }
    }
    if (m_Boss != nullptr && !m_Boss->State().dead && m_Boss->State().stats.IsDead()) {
        m_Boss->Kill();
        m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kBossViewId, "death"}); // A
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
    TickSkill();  // A3: activate + advance cooldown BEFORE the weapon (so the mirror sees in_skill).
    TickWeapon();
    EmitAttackEvents(); // A: enemy/boss "attack" cues (the OnShootTicks ran in Scheduler.Tick).
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
void Simulation::SetBoss(float baseShootCd, glm::vec2 spawn, int maxHp, int roomId, int seed,
                         const std::string &bossId) {
    // (d) dispatch: BrainFactory routes bossId -> its brain adapter. The default
    // "BossAI01" keeps the legacy path byte-identical.
    m_Boss = BrainFactory::MakeBossPtr(bossId, baseShootCd, spawn, maxHp, seed);
    m_Boss->MutableState().roomId = roomId;
    m_Boss->Activate(m_Scheduler, m_FireIntents); // asleep until WakeByRoom().
}
void Simulation::EquipWeapon(const Game::WeaponDef &def, const std::string &weaponId,
                             int seed) {
    m_Weapon.emplace(BrainFactory::MakeWeapon(def, weaponId, seed)); // cold rebuild
}
void Simulation::SetPlayerSkill(const std::string &charId, float skillCd, float inSkillTime) {
    // B1-P4a (d) dispatch: charId -> per-hero ICharSkill adapter. C01 (mirror) + C02
    // (dash) are faithful; c03..c13 are gate+cooldown-only stubs. The skill starts
    // READY (each brain ctor). Single owned member, parallel to m_Weapon.
    m_Skill = MakeCharSkill(charId, skillCd, inSkillTime);
}

} // namespace Game::Sim
