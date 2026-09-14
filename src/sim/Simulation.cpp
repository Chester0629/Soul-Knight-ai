#include "sim/Simulation.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "combat/Buff.hpp"
#include "combat/CharSkillC01.hpp"
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

    // --- C01 skill effect: the second hand mirrors the primary attack 0.1s LATER ---
    // While the ultimate is active, the second hand MIRRORS the primary attack
    // (CharSkillC01::RoleAtk -> mirrorSecondHand). FAITHFUL timing: the real c01 second
    // hand is fired via a 0.1s-delayed Invoke ("Hand2Atk", kHand2AttackDelay), NOT a
    // same-frame clone -- so we copy THIS step's primary shots and schedule them on the
    // deterministic sim Scheduler kHand2AttackDelay (-> 5 fixed ticks) later. (The
    // separate-gun cadence + the actual hand transform/animation remain owner/
    // presentation; the perpendicular offset is a presentation placeholder for the
    // second-hand muzzle position -- not RE-recoverable, the get_transform tail-call is
    // truncated. Full c01 hand-visual fidelity = B1b.)
    if (m_Skill != nullptr && shots > 0) {
        const ICharSkill::AtkEffect d =
            m_Skill->RoleAtk(/*pressDown=*/m_Input.firing, /*standingOnItem=*/false);
        if (d.mirrorSecondHand && d.secondHandValue) {
            constexpr float kSecondHandOffsetPx = 12.0F; // placeholder hand offset (presentation).
            // Build the mirrored intents from THIS step's primary shots, offset
            // perpendicular to aim as the second-hand muzzle stand-in (value-copied, so
            // NO new RNG draw and no dependency on the buffer after this step).
            std::vector<FireIntent> mirror(
                m_FireIntents.begin() + static_cast<std::ptrdiff_t>(before),
                m_FireIntents.begin() + static_cast<std::ptrdiff_t>(before + shots));
            for (FireIntent &mi : mirror) {
                const glm::vec2 perp{-mi.dir.y, mi.dir.x};
                mi.origin += perp * kSecondHandOffsetPx;
            }
            // Fire them kHand2AttackDelay (0.1s -> 5 fixed ticks) LATER via the
            // deterministic Scheduler. The callback appends the deferred intents (drained
            // that later step into bullets) and emits the second-hand muzzle cue THEN.
            // Energy-free across the delay: the deferred intents are NEVER added to
            // m_PlayerShotsLastAdvance, so the second hand does not double the energy
            // spend. The whole path is m_Skill-gated, so with no skill set nothing is
            // scheduled and the SimulationTest replay stays byte-identical.
            m_Scheduler.Invoke(
                Scheduler::SecondsToTicks(Game::CharSkillC01::kHand2AttackDelay),
                [this, mirror = std::move(mirror)]() {
                    for (const FireIntent &mi : mirror) {
                        m_FireIntents.push_back(mi);
                        m_Events.push_back(
                            SimEvent{SimEventType::AnimTrigger, kPlayerViewId, "fire"});
                    }
                });
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
        if (r.outcome == ICharSkill::TriggerOutcome::Activated) {
            // M3e: statue blessings (BuffStatue.UseSkill) hang off the skill press.
            m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kPlayerViewId, "skill_use"});
        }
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
// P3.1: apply (or refresh) a status-effect buff on one entity. Grants the Armor
// buff's flat armor bonus exactly once (on first apply, not on a same-kind
// refresh); TickBuffs() reverts exactly this much when the buff ends.
void Simulation::ApplyBuff(std::uint32_t entityId, const Game::Combat::BuffParams &params) {
    Game::Combat::BuffList *list = nullptr;
    Game::CombatStats *stats = nullptr;
    int *armorGranted = nullptr;
    if (entityId == kPlayerViewId) {
        list = &m_PlayerBuffs;
        stats = &m_PlayerStats;
        armorGranted = &m_PlayerArmorGranted;
    } else if (entityId == kBossViewId) {
        if (m_Boss == nullptr) {
            return;
        }
        list = &m_Boss->MutableState().buffs;
        stats = &m_Boss->MutableState().stats;
        armorGranted = &m_Boss->MutableState().buffArmorGranted;
    } else if (entityId < m_Enemies.size()) {
        list = &m_Enemies[entityId]->MutableState().buffs;
        stats = &m_Enemies[entityId]->MutableState().stats;
        armorGranted = &m_Enemies[entityId]->MutableState().buffArmorGranted;
    } else {
        return; // unknown / nonexistent entity: no-op.
    }

    const bool hadArmor = list->Has(Game::Combat::BuffKind::Armor);
    list->Apply(params);
    if (params.kind == Game::Combat::BuffKind::Armor && !hadArmor) {
        // INFERRED (T-32): record what was ACTUALLY added, not what was requested.
        // CombatStats::RestoreArmor caps a positive delta at maxArmor, so an entity at
        // full armor gains 0 from a +1 Armor buff; recording the requested +1 would let
        // the expiry path subtract armor that was never granted.
        const int armorBefore = stats->armor;
        stats->RestoreArmor(params.armorDelta);
        *armorGranted = stats->armor - armorBefore;
    }
    m_Events.push_back(SimEvent{
        SimEventType::AnimTrigger, entityId,
        "buff_applied:" + std::string(Game::Combat::BuffKindName(params.kind)) + ":" +
            std::to_string(params.durationSec)});
}

// P3.2: bullet-trigger wiring -- if the bullet that just landed carries a buff
// (b.hasBuff, resolved once at EquipWeapon time from BulletDef::buffId), roll it
// against buffProbability and, on success, apply it to the target that was just
// hit. Called once per landed hit, AFTER the damage/repel for that hit -- so a
// bullet that kills its target on this same hit still applies its buff (matches
// ResolveHits' existing "all in-flight bullets land" determinism: the buff roll
// draws from the same m_HitRng stream, in the same fixed per-bullet order, as the
// crit roll that precedes it).
void Simulation::RollBulletBuff(const BulletState &b, std::uint32_t targetId) {
    if (!b.hasBuff) {
        return;
    }
    if (b.buffProbability < 1.0F) {
        // FAITHFUL: BuffEffectTrigger$$TriggerWith (game_typed.c:86608-86645) draws
        // UnityEngine.Random.Range(0,100) and skips applying the buff when
        // `probability <= roll` -- i.e. probability is a 0..100 percent chance.
        // RGLaserBuff's buff_probability is rolled the same way (T-33: no visible
        // compare to mirror in its own decompile, see bullets.json's "buff._why").
        // RGBulletBuffTrigger/RGSwordBuffTrigger/RGArrowBuffTrigger never reach this
        // branch (their rows carry probability 1.0 exactly) -- faithfully, those
        // triggers never roll at all in the decompile.
        const int roll = m_HitRng.Range(0, 100);
        const float percent = b.buffProbability * 100.0F;
        if (!(percent > static_cast<float>(roll))) {
            return; // probability <= roll: skip, matching the decompile's early-return.
        }
    }
    ApplyBuff(targetId, b.buffParams);
}

// P3.1: advance every entity's BuffList by one fixed step: apply Fire/Poison DoT
// through the existing Damage.hpp path (armor-then-hp for the player, straight hp
// for enemies/boss -- same split ResolveHits() uses, just skipping the crit roll/
// repel since a DoT tick is not a bullet hit), cache the combined speed multiplier
// for MoveControllers(), revert the Armor buff's flat bonus on end, and emit the
// "Buff: <kind> ended" cue.
void Simulation::TickBuffs() {
    auto endedEvent = [this](std::uint32_t id, Game::Combat::BuffKind k) {
        m_Events.push_back(SimEvent{SimEventType::AnimTrigger, id,
                                    "buff_ended:" + std::string(Game::Combat::BuffKindName(k))});
    };

    // --- player ---
    {
        const Game::Combat::BuffList::TickResult r = m_PlayerBuffs.Tick(kFixedStepSeconds);
        m_PlayerSpeedMul = r.speedMul;
        m_PlayerFrozen = r.frozen;
        if (r.damage > 0 && m_Input.playerAlive) {
            Game::Combat::HitResult hr;
            hr.finalDamage = r.damage;
            Game::Combat::ApplyToPlayer(m_PlayerStats, hr, /*canHurt=*/true);
        }
        for (const Game::Combat::BuffKind k : r.ended) {
            if (k == Game::Combat::BuffKind::Armor) {
                // INFERRED (T-32): the decompile does not specify what happens to a
                // temporary armor buff's bonus when the entity's armor state changed
                // (took damage) while the buff was active. Rule chosen here: remove
                // min(granted, current) -- never more than the entity still has. It is
                // the least-surprising option: it cannot push armor negative and cannot
                // double-penalize a hit that already consumed the granted armor.
                const int toRemove = std::min(m_PlayerArmorGranted, m_PlayerStats.armor);
                m_PlayerStats.RestoreArmor(-toRemove);
                m_PlayerArmorGranted = 0;
            }
            endedEvent(kPlayerViewId, k);
        }
    }

    // --- enemies ---
    for (std::size_t ei = 0; ei < m_Enemies.size(); ++ei) {
        EntityState &st = m_Enemies[ei]->MutableState();
        const Game::Combat::BuffList::TickResult r = st.buffs.Tick(kFixedStepSeconds);
        st.buffSpeedMul = r.speedMul;
        st.buffFrozen = r.frozen;
        if (r.damage > 0 && !st.dead) {
            Game::Combat::HitResult hr;
            hr.finalDamage = r.damage;
            Game::Combat::ApplyToEnemy(st.stats, hr, /*canHurt=*/true);
        }
        for (const Game::Combat::BuffKind k : r.ended) {
            if (k == Game::Combat::BuffKind::Armor) {
                // INFERRED (T-32): same rule as the player path above -- remove
                // min(granted, current), never more armor than the entity still has.
                const int toRemove = std::min(st.buffArmorGranted, st.stats.armor);
                st.stats.armor -= toRemove; // >= 0 by construction, no extra floor needed.
                st.buffArmorGranted = 0;
            }
            endedEvent(static_cast<std::uint32_t>(ei), k);
        }
    }

    // --- boss ---
    if (m_Boss != nullptr) {
        EntityState &st = m_Boss->MutableState();
        const Game::Combat::BuffList::TickResult r = st.buffs.Tick(kFixedStepSeconds);
        st.buffSpeedMul = r.speedMul;
        st.buffFrozen = r.frozen;
        if (r.damage > 0 && !st.dead) {
            Game::Combat::HitResult hr;
            hr.finalDamage = r.damage;
            Game::Combat::ApplyToEnemy(st.stats, hr, /*canHurt=*/true);
        }
        for (const Game::Combat::BuffKind k : r.ended) {
            if (k == Game::Combat::BuffKind::Armor) {
                // INFERRED (T-32): same rule as the player path above -- remove
                // min(granted, current), never more armor than the entity still has.
                const int toRemove = std::min(st.buffArmorGranted, st.stats.armor);
                st.stats.armor -= toRemove; // >= 0 by construction, no extra floor needed.
                st.buffArmorGranted = 0;
            }
            endedEvent(kBossViewId, k);
        }
    }
}

void Simulation::MoveControllers() {
    for (auto &e : m_Enemies) {
        if (e->State().dead) {
            continue;
        }
        // P3.1: buffSpeedMul folds in Ice/Electric freeze (0.0) and ChangeSpeed's
        // slow (< 1.0); 1.0 (no buff) leaves movement byte-identical to before.
        const glm::vec2 vel = e->ComputeVelocity() * e->State().buffSpeedMul;
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
        // P3.1: buffSpeedMul folds in Ice/Electric freeze (0.0); 1.0 (no buff)
        // leaves boss movement byte-identical to before.
        const glm::vec2 vel = moveDir * BossController::kSpeed * m_Boss->State().buffSpeedMul;
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
                RollBulletBuff(b, static_cast<std::uint32_t>(ei)); // P3.2
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
                RollBulletBuff(b, kBossViewId); // P3.2
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
                // P3.2: enemy bullets never carry a buff today (EnemyGunDef has no
                // bullet-prefab link into bullets.json's buff-carrying rows -- NOT
                // PORTED, see the P3.2 report); this call is a no-op (b.hasBuff is
                // always false here) kept for symmetry / future wiring.
                RollBulletBuff(b, kPlayerViewId);
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
    TickBuffs();        // P3.1: advance buffs (DoT + speed/freeze cache) BEFORE movement uses them.
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
    // ENEMY RE-KEY: FAITHFUL @ enemies.csv -- seed the real per-species maxHp when
    // the def carries one; kSliceEnemyHp is only the fallback for a def with no
    // species data (e.g. a default-constructed test EnemyDef{}), so old tests that
    // pass an empty EnemyDef{} keep their old flat-HP behaviour unchanged.
    const int seedHp = def.maxHp > 0 ? def.maxHp : kSliceEnemyHp;
    ec->MutableState().stats.hp = seedHp;
    ec->MutableState().stats.maxHp = seedHp;
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
                             int seed, const Game::GameData *data) {
    m_Weapon.emplace(BrainFactory::MakeWeapon(def, weaponId, seed, data)); // cold rebuild
}
void Simulation::SetPlayerSkill(const std::string &charId, float skillCd, float inSkillTime) {
    // B1-P4a (d) dispatch: charId -> per-hero ICharSkill adapter. C01 (mirror) + C02
    // (dash) are faithful; c03..c13 are gate+cooldown-only stubs. The skill starts
    // READY (each brain ctor). Single owned member, parallel to m_Weapon.
    m_Skill = MakeCharSkill(charId, skillCd, inSkillTime);
}

void Simulation::KillAllHostiles() {
    for (auto &e : m_Enemies) {
        if (!e->State().dead) {
            e->MutableState().stats.hp = 0;
        }
    }
    if (m_Boss != nullptr && !m_Boss->State().dead) {
        m_Boss->MutableState().stats.hp = 0;
    }
}

} // namespace Game::Sim
