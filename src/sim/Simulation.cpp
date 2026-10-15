#include "sim/Simulation.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "combat/Buff.hpp"
#include "combat/CharSkillC01.hpp"
#include "combat/Damage.hpp"
#include "sim/BodyMove.hpp"
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

std::vector<Simulation::BulletImpact> Simulation::DrainImpacts() {
    std::vector<BulletImpact> out;
    out.swap(m_Impacts);
    return out;
}

namespace {
/// PORT CHOICE (T-66): the bullet that stands in for a species whose prefab fires no
/// RGBullet-family bullet (melee sword/spear prefabs, lasers, explosions: 54 of 146 species have
/// no bullets.json bullet; the gate lists them). bullet_e_1 (16x16 red orb, box 0.3 x 0.3) is
/// the most widely shared enemy bullet. The scene logs each substituted species.
constexpr const char *kGenericEnemyBulletId = "bullet_e_1";
} // namespace

BulletLaunch Simulation::LaunchFor(const Game::EnemyDef &def) const {
    if (m_Data == nullptr) {
        return BulletLaunch{};
    }
    BulletLaunch l = MakeBulletLaunch(*m_Data, def.bulletPrefab);
    if (l.bulletIdx < 0) {
        l = MakeBulletLaunch(*m_Data, kGenericEnemyBulletId);
    }
    return l;
}

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
    // --- P3.3.b: C04's melee sword cut. RoleAtkMelee is a no-op default (all-false)
    // for every hero except C04, so this block is behaviourally invisible for the
    // rest of the roster -- suppressFire stays false and the normal weapon path
    // below runs exactly as before. For C04, in_skill_effect redirects the SAME
    // press that would have fired the gun into one big cut instead (FAITHFUL:
    // C04Controller__RoleAtk never reaches hand.SetAttack on that branch).
    bool suppressFire = false;
    if (m_Skill != nullptr) {
        const ICharSkill::MeleeAtkEffect m = m_Skill->RoleAtkMelee(
            /*pressDown=*/m_Input.firing, /*standingOnItem=*/false, m_PlayerAtk,
            /*skillStrengthen=*/false); // PORT CHOICE (T-38): skill_strengthen has no
                                        // data source yet (RoleAttributePlayer.skill_
                                        // strengthen is not wired into CharacterDef /
                                        // characters.json), so it is hardcoded false
                                        // here -- every C04 cut in the sim uses the
                                        // un-strengthened bonus (+3, kCutBonusNormal)
                                        // until that stat is threaded through. This is
                                        // a SEPARATE debt from T-36 (C04's melee reach/
                                        // arc hitbox, SimConfig.hpp) -- do not conflate.
        if (m.doCut) {
            ApplyMeleeCut(m.cutDamage);
        }
        suppressFire = m.suppressWeaponFire;
    }
    if (!m_Weapon.has_value() || suppressFire) {
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
    // Activation (edge trigger): the skill button tries to enter the ultimate via
    // the (d) adapter, but ONLY when a press-edge is PENDING (item 1 fix, R18/Codex
    // block; R19 fix for the zero-substep drop -- see m_SkillPressPending and
    // Advance()). The false->true edge on the raw held-key level (GameScene forwards
    // IsKeyDown unedited) is now detected in Advance(), at CALL granularity, not
    // here -- a short dt can make Advance() run ZERO fixed Step()s, so detecting the
    // edge only here (against m_Input.skill, fixed-step granularity) could silently
    // drop a press the next Advance() call overwrites before any Step() saw it.
    // Here we only CONSUME the latch, exactly once: clearing it (rather than
    // re-deriving skillEdge from m_Input.skill) is what keeps a single press
    // activating exactly once whether it spans multiple fixed sub-steps of ONE
    // Advance() call (40ms press == 2 sub-steps @ 20ms -- the flag is already false
    // by the 2nd sub-step) or is held across MANY separate Advance() calls (the flag
    // is only re-armed by a fresh false->true transition of the raw input in
    // Advance(), never by "still true").
    const bool skillEdge = m_SkillPressPending;
    m_SkillPressPending = false;
    if (skillEdge) {
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
    // P3.3.c: heroes whose ultimate spawns a world effect (C03 thunder chain, C07 bat
    // swarm, C11 holy light, C12 melee cut / wolf summon, C05 bottle volley) advance it
    // here against a read-only world snapshot; every other hero skips this entirely
    // (NeedsWorld() defaults false), so their behaviour -- and the replay traces -- are
    // untouched.
    if (m_Skill->NeedsWorld()) {
        const ICharSkill::SkillWorldView view = BuildSkillWorld();
        ICharSkill::SkillEffectOut out;
        m_Skill->TickEffects(view, out);
        ApplySkillEffects(out);
    }
    // A3 cooldown bridge: publish the 0..1 charge into the player stats the shell
    // pulls back each frame (-> CombatStats::skillCdProgress -> HUD; A4 renders it).
    m_PlayerStats.skillCdProgress = m_Skill->CooldownProgress();
}

// P3.3.c: the read-only snapshot a world-effect skill sees. Hostiles are the LIVE,
// AWAKE enemies (view id == enemy index) then the boss (kBossViewId) -- the entities in
// the player's room; a Physics2D.CircleCastAll on the enemy layer could also see a
// sleeping enemy of another room inside the radius, the sim only exposes awake ones
// (part of T-41). Wall line-of-sight (Physics2D.Linecast, layer "Wall") is
// approximated by sampling WorldCollision::Blocks along the segment (PORT CHOICE
// (T-41): the sim's world is a point/circle query, not a raycast).
ICharSkill::SkillWorldView Simulation::BuildSkillWorld() const {
    ICharSkill::SkillWorldView v;
    v.dtSec = kFixedStepSeconds;
    v.playerPos = m_Input.playerPos;
    v.aimDir = m_Input.aimDir;
    v.playerHp = m_PlayerStats.hp;
    v.playerMaxHp = m_PlayerStats.maxHp;
    for (std::size_t i = 0; i < m_Enemies.size(); ++i) {
        const EntityState &s = m_Enemies[i]->State();
        if (!s.dead && s.awake && !s.stats.IsDead()) {
            v.hostiles.push_back({static_cast<std::uint32_t>(i), s.pos});
        }
    }
    if (m_Boss != nullptr) {
        const EntityState &s = m_Boss->State();
        if (!s.dead && s.awake && !s.stats.IsDead()) {
            v.hostiles.push_back({kBossViewId, s.pos});
        }
    }
    const WorldCollision *world = m_World;
    v.lineBlocked = [world](glm::vec2 a, glm::vec2 b) {
        constexpr float kSampleStepPx = 8.0F; // PORT CHOICE (T-41).
        constexpr float kSampleRadiusPx = 1.0F;
        const glm::vec2 d = b - a;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        const int n = std::max(1, static_cast<int>(std::ceil(len / kSampleStepPx)));
        for (int i = 0; i <= n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(n);
            if (world->Blocks(a + d * t, kSampleRadiusPx)) {
                return true;
            }
        }
        return false;
    };
    return v;
}

// P3.3.c: apply what a skill's world effect asked for. Skill damage is a straight
// RGEController.GetHurt(damage, source) (game_typed.c has no crit roll on these paths),
// attributed DeathSource::PlayerSkill so Cull credits the kill to the player.
void Simulation::ApplySkillEffects(const ICharSkill::SkillEffectOut &out) {
    for (const ICharSkill::SkillEffectOut::Hit &h : out.hits) {
        if (h.damage <= 0) {
            continue;
        }
        Game::Combat::HitResult hr;
        hr.finalDamage = h.damage;
        if (h.targetId == kBossViewId) {
            if (m_Boss != nullptr && !m_Boss->State().dead) {
                Game::Combat::ApplyToEnemy(m_Boss->MutableState().stats, hr, true);
                m_Boss->MutableState().SetDeathSource(DeathSource::PlayerSkill);
                m_Boss->OnHurt(m_Boss->State().stats.hp, m_Boss->State().stats.maxHp);
                m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kBossViewId, "hurt"});
            }
        } else if (h.targetId < m_Enemies.size() && !m_Enemies[h.targetId]->State().dead) {
            auto &e = m_Enemies[h.targetId];
            Game::Combat::ApplyToEnemy(e->MutableState().stats, hr, true);
            e->MutableState().SetDeathSource(DeathSource::PlayerSkill);
            m_Events.push_back(SimEvent{SimEventType::AnimTrigger, h.targetId, "hurt"});
        }
    }
    if (out.meleeCutDamage > 0) {
        ApplyMeleeCut(out.meleeCutDamage); // RGHand.AtkCut -- DeathSource::PlayerCut.
    }
    if (out.healHp > 0) {
        m_PlayerStats.Heal(out.healHp);
    }
    if (out.restoreEnergy > 0) {
        m_PlayerEnergyGrantedLastAdvance += out.restoreEnergy;
    }
    for (const std::string &cue : out.cues) {
        m_Events.push_back(
            SimEvent{SimEventType::AnimTrigger, kPlayerViewId, "skill_effect:" + cue});
    }
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
void Simulation::ApplyBuff(std::uint32_t entityId, const Game::Combat::BuffParams &params,
                           bool fromPlayer) {
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
    list->Apply(params, fromPlayer);
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

// FAITHFUL: BuffEffectTrigger$$TriggerWith @ game_typed.c:86608-86645 (disasm
// 00ae97dc). The decompiled body, in full:
//
//   fVar6 = this->probability;                       // 0..100 PERCENT
//   // the fpscr bit test below is the compiler's encoding of the comparison
//   // `probability <= 0.0f` WITH the NaN case folded into the same branch:
//   // bit 0x1f = (probability < 0), bit 0x1e = (probability == 0),
//   // bit 0x1c = isnan(probability).
//   if (probability < 0 || probability == 0 || isnan(probability)) {
//       if (!isCritic) {
//           bullet = RGBulletEffectTrigger__get_bullet(this);  // throws if null
//           if (bullet->camp == 0) {
//               return;                              // <-- the ONLY skip here
//           }
//       }
//       // isCritic, or camp != 0 -> fall through and APPLY
//   } else {
//       roll = UnityEngine.Random.Range(0, 100);      // max EXCLUSIVE -> 0..99
//       if (probability <= (float)roll) {
//           return;                                   // skip
//       }
//   }
//   ... AddBuff ...
//
// So the zero/negative/NaN-probability case is NOT "never applies": it applies
// on a CRITICAL hit, and it applies for any bullet whose camp != 0, and it is
// skipped only when the hit is not a crit AND the bullet's camp == 0 (the
// player's own camp). Range(0,100) is max-exclusive (see RGRandom::Range(int,int)
// doc), so the roll is 0..99 and any probability >= 100 always applies.
bool Simulation::ShouldApplyBulletBuff(float probability, bool isCritic, int camp,
                                       Game::RGRandom &rng) {
    if (!(probability > 0.0F)) {
        // Covers probability < 0, probability == 0 AND NaN in one test -- exactly
        // the branch the fpscr bit test selects. No RNG draw on this path
        // (faithful: TriggerWith does not call Random.Range here).
        return isCritic || camp != 0;
    }
    const int roll = rng.Range(0, 100);
    return probability > static_cast<float>(roll);
}

// P3.2: bullet-trigger wiring -- if the bullet that just landed carries a buff
// (b.hasBuff, resolved once at EquipWeapon time from BulletDef::buffId), run the
// decompiled BuffEffectTrigger decision (ShouldApplyBulletBuff above) and, on
// success, apply it to the target that was just hit. Called once per landed hit,
// AFTER the damage/repel for that hit -- so a bullet that kills its target on
// this same hit still applies its buff (matches ResolveHits' existing "all
// in-flight bullets land" determinism: the buff roll draws from the same m_HitRng
// stream, in the same fixed per-bullet order, as the crit roll that precedes it).
//
// @p isCritic is THIS hit's resolved crit flag (Combat::HitResult::isCrit, the
// same roll that scaled the damage just applied) -- the decompile reads its own
// `isCritic` parameter, which RGBulletTrigger passes down from the same crit
// decision. The camp is the BULLET's camp (BulletState::camp), matching
// TriggerWith's `get_bullet()->camp`.
//
// bullets.json stores probability as a 0..1 FRACTION (build_gamedata.py divides
// the serialized percent by 100), so it is scaled back to the decompile's 0..100
// percent here. Rows whose probability is the INFERRED (T-33) 1.0 -- the
// RGBulletBuffTrigger / RGSwordBuffTrigger / RGArrowBuffTrigger families, which
// serialize no probability at all -- therefore take the roll branch at 100
// percent and always apply (roll is 0..99).
void Simulation::RollBulletBuff(const BulletState &b, std::uint32_t targetId, bool isCritic) {
    if (!b.hasBuff) {
        return; // no buff carried: no decision, no RNG draw.
    }
    if (!ShouldApplyBulletBuff(b.buffProbability * 100.0F, isCritic, b.camp, m_HitRng)) {
        return;
    }
    // item 5/2 (R19): b.camp == 0 is the PLAYER's own camp (see ResolveHits' "Player
    // bullet -> enemies, then boss" branch above) -- a buff carried by a player
    // bullet is a player-sourced application, so its later DoT ticks must
    // attribute EntityState::deathSource to DeathSource::PlayerDot, same as the
    // bullet's own direct hit already does (DeathSource::PlayerBullet).
    ApplyBuff(targetId, b.buffParams, /*fromPlayer=*/b.camp == 0);
}

// P3.3.b: route an incoming player hit through the skill's OnPlayerHurt hook first
// (C08's shield absorb; every other hero's default is an exact pass-through --
// absorbed=false, damageToApply==the raw finalDamage -- so this is behaviourally
// invisible for them). @p hr is taken BY VALUE so the caller's copy is untouched.
void Simulation::ApplyDamageToPlayer(Game::Combat::HitResult hr) {
    if (m_Skill != nullptr) {
        const ICharSkill::HurtOutcome ho = m_Skill->OnPlayerHurt(hr.finalDamage, /*awake=*/true);
        if (ho.absorbed) {
            return; // C08's shield ate it (or the silent !awake no-op) -- no HP/armor change.
        }
        hr.finalDamage = ho.damageToApply;
    }
    Game::Combat::ApplyToPlayer(m_PlayerStats, hr, true);
}

// P3.3.b: C04's melee sword cut (see SimConfig.hpp T-36 for the reach/arc port
// choice). Straight damage, no crit/repel (the decomp shows no crit roll on this
// path); marks EntityState::deathSource (DeathSource::PlayerCut, item 2/R19) so
// Cull can attribute a resulting death to ICharSkill::OnEnemyKilled (C04's
// kill-combo), and emits the same "hurt" AnimTrigger a bullet hit would
// (presentation parity).
void Simulation::ApplyMeleeCut(int damage) {
    if (damage <= 0) {
        return;
    }
    const glm::vec2 origin = m_Input.playerPos;
    const glm::vec2 aim = Normalize(m_Input.aimDir);
    const float cosHalfAngle = std::cos(kMeleeCutHalfAngleDeg * kDegToRad);
    auto inReach = [&](glm::vec2 targetPos) {
        const glm::vec2 d = targetPos - origin;
        const float distSq = d.x * d.x + d.y * d.y;
        if (distSq > kMeleeCutReachPx * kMeleeCutReachPx) {
            return false;
        }
        if (distSq <= 1e-6F) {
            return true; // on top of the player: always in the arc.
        }
        const float dist = std::sqrt(distSq);
        const glm::vec2 dn = d / dist;
        return (dn.x * aim.x + dn.y * aim.y) >= cosHalfAngle;
    };

    Game::Combat::HitResult hr;
    hr.finalDamage = damage;
    for (std::size_t ei = 0; ei < m_Enemies.size(); ++ei) {
        auto &e = m_Enemies[ei];
        if (e->State().dead || !inReach(e->State().pos)) {
            continue;
        }
        Game::Combat::ApplyToEnemy(e->MutableState().stats, hr, true);
        e->MutableState().SetDeathSource(DeathSource::PlayerCut); // item 2 (R19)
        m_Events.push_back(
            SimEvent{SimEventType::AnimTrigger, static_cast<std::uint32_t>(ei), "hurt"});
    }
    if (m_Boss != nullptr && !m_Boss->State().dead && inReach(m_Boss->State().pos)) {
        Game::Combat::ApplyToEnemy(m_Boss->MutableState().stats, hr, true);
        m_Boss->MutableState().SetDeathSource(DeathSource::PlayerCut); // item 2 (R19)
        m_Boss->OnHurt(m_Boss->State().stats.hp, m_Boss->State().stats.maxHp);
        m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kBossViewId, "hurt"});
    }
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
            ApplyDamageToPlayer(hr); // P3.3.b: C08's shield can absorb a DoT tick too.
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
        // item 2 (R19 fix): apply the two DoT sources as SEPARATE damage events,
        // non-player FIRST then player -- documented rule, see
        // Combat::BuffList::TickResult and EntityState::DeathSource. Skip the 2nd
        // (player) portion once the entity is already dead from the 1st: it cannot
        // be the tick that killed it, so it must not overwrite the attribution the
        // 1st portion already earned (SetDeathSource would ignore it anyway once
        // Forced, but here we want the FIRST lethal tick's source to stick, not
        // just "not Forced").
        if ((r.damageFromOther > 0 || r.damageFromPlayer > 0) && !st.dead) {
            if (r.damageFromOther > 0) {
                Game::Combat::HitResult hr;
                hr.finalDamage = r.damageFromOther;
                Game::Combat::ApplyToEnemy(st.stats, hr, /*canHurt=*/true);
                st.SetDeathSource(DeathSource::Other);
            }
            if (r.damageFromPlayer > 0 && !st.stats.IsDead()) {
                Game::Combat::HitResult hr;
                hr.finalDamage = r.damageFromPlayer;
                Game::Combat::ApplyToEnemy(st.stats, hr, /*canHurt=*/true);
                st.SetDeathSource(DeathSource::PlayerDot);
            }
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
        if ((r.damageFromOther > 0 || r.damageFromPlayer > 0) && !st.dead) {
            // item 2 (R19): see the enemy loop above -- same non-player-first rule.
            if (r.damageFromOther > 0) {
                Game::Combat::HitResult hr;
                hr.finalDamage = r.damageFromOther;
                Game::Combat::ApplyToEnemy(st.stats, hr, /*canHurt=*/true);
                st.SetDeathSource(DeathSource::Other);
            }
            if (r.damageFromPlayer > 0 && !st.stats.IsDead()) {
                Game::Combat::HitResult hr;
                hr.finalDamage = r.damageFromPlayer;
                Game::Combat::ApplyToEnemy(st.stats, hr, /*canHurt=*/true);
                st.SetDeathSource(DeathSource::PlayerDot);
            }
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

// P2.18 (PORT CHOICE T-61/T-62): movement is wall-gated AND swept, and a body that overlaps a
// blocker is pushed out first.
//   1. Depenetrate: every live hostile that overlaps a blocker at the START of the tick (spawned
//      inside one, teleported by a brain, caught by a door that just sealed, knocked in) is pushed
//      to the nearest free position -- INFERRED to match what Box2D does to the Dynamic
//      Rigidbody2D bodies (Kinematic / body-less prefabs are the exceptions; exit POSITIONS are the
//      T-61 approximation). Without it the move gate below (swept or not) refuses every step of a body that starts overlapping, pinning it for good. It runs
//      BEFORE the step-start snapshot, so the push is a reposition, not a motion the swept bullet
//      test would credit. One cheap Blocks() query per entity when nothing overlaps.
//   2. MoveBody: the same axis-separated slide as before, with each axis move tested as a swept
//      segment (WorldCollision::MoveBlocked, exact in GameScene) in sub-steps of at most one body
//      radius, so neither a long knockback / charge nor a short graze can cross a blocker.
void Simulation::MoveControllers() {
    for (auto &e : m_Enemies) {
        if (e->State().dead) {
            continue;
        }
        EntityState &st = e->MutableState();
        st.pos = m_World->Depenetrate(st.pos, st.geom.bodyRadiusPx);
    }
    if (m_Boss != nullptr && !m_Boss->State().dead) {
        EntityState &st = m_Boss->MutableState();
        st.pos = m_World->Depenetrate(st.pos, st.geom.bodyRadiusPx);
    }
    // T-57: snapshot every hurtbox owner's step-start position for ResolveHits' swept test.
    m_EnemyStepStart.resize(m_Enemies.size());
    for (std::size_t i = 0; i < m_Enemies.size(); ++i) {
        m_EnemyStepStart[i] = m_Enemies[i]->State().pos;
    }
    if (m_Boss != nullptr) {
        m_BossStepStart = m_Boss->State().pos;
    }
    for (auto &e : m_Enemies) {
        if (e->State().dead) {
            continue;
        }
        // P3.1: buffSpeedMul folds in Ice/Electric freeze (0.0) and ChangeSpeed's
        // slow (< 1.0); 1.0 (no buff) leaves movement byte-identical to before.
        const glm::vec2 vel = e->ComputeVelocity() * e->State().buffSpeedMul;
        const float bodyR = e->State().geom.bodyRadiusPx; // prefab body circle (data)
        // Axis-separated wall slide (same shape as the GameScene player pattern), swept.
        const glm::vec2 pos = MoveBody(*m_World, e->State().pos,
                                       glm::vec2{vel.x * kFixedStepSeconds, vel.y * kFixedStepSeconds},
                                       bodyR);
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
        const float bodyR = m_Boss->State().geom.bodyRadiusPx; // prefab body circle (data)
        const glm::vec2 pos = MoveBody(*m_World, m_Boss->State().pos,
                                       glm::vec2{vel.x * kFixedStepSeconds, vel.y * kFixedStepSeconds},
                                       bodyR);
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
// T-57 (swept collision). A bullet's path during one fixed step is the segment
// prevPos -> prevPos + vel * dt. Since P3.0 (1 unit = 32 px) weapon bullets reach 64 units/s =
// ~41 px per step, more than a hurtbox is wide, so testing only the end-of-step position
// lets a bullet jump clean over a target (Codex R30: weapon_033 vs a head-on e_alien01) or a
// 1-cell wall. Instead, ONE time scale t in [0,1] (0 = step start, 1 = step end) is shared by
// every moving thing of the step, and the bullet keeps its FULL path:
//   * IntegrateBullets: only moves the bullet (prevPos, pos = full end of step, lifetime);
//   * ResolveHits, per bullet in ascending id order: (1) the WALL fraction tw of the full path
//     (WorldCollision::SweepBlocked, -1 = free), (2) the TARGET sweep on the same full path
//     (BodyGeom::HurtSweepBeforeWall: relative motion vs the hurtbox grown by the bullet
//     radius), accepted only when t_hit <= tw, (3) a bullet that survived (missed, or pierced)
//     and has a wall on its path is consumed at the contact point (DamageObstacle).
// TIE RULE: t_hit == tw is TARGET-FIRST (the target is struck, then the wall stops the bullet).
// The wall fraction is the sampled contact rounded UP by the bisection, so a near-tie within
// ~1/256 of one sample gap (< 0.1 px of bullet travel) also resolves target-first.
// Wall query and DamageObstacle happen per bullet IN ORDER (not as a first pass over all
// bullets), so a box broken by bullet A is already gone when bullet B of the same step
// queries the wall -- the pre-swept behaviour (IntegrateBullets did query + damage per bullet).
// canThrough bullets skip the wall sweep exactly as they skipped the old wall test. The step
// order (melee cut -> DoT -> bullets -> Cull) and DeathSource locking are unchanged.
// PORT CHOICE (T-57): the default WorldCollision::SweepBlocked ASSUMES the path start is free
// (it was the previous step's end, already tested; a bullet spawned inside a wall is reported
// stopped within the first sample gap) and samples Blocks() every <= max(radius, 1) px along
// the path, then bisects the first blocked gap (8 halvings), because the world interface is a
// circle query, not geometry. With spacing <= r the sampled discs overlap, and a blocker is
// missed only if it intrudes less than ~0.134 * r (about 1 px for an 8 px bullet) into the
// swept capsule between two samples, i.e. a pure graze; a wall that spans the path's width can
// never be tunnelled (it blocks an interval of at least 2r >= spacing along the path). The
// contact point is accurate to 1/256 of a sample gap. A bullet spawned inside a wall AND a
// target (start overlap, t = 0 <= tw) hits the target first, then the wall consumes it.
namespace {
/// P3.0c: the DamageObstacle probe of a wall contact, relative to the bullet origin. In the exact world
/// (GameScene) it is a radius-1 circle on the blocker point nearest the box centre; in a circle-only
/// world it is the covering-row circle that touched first (T-64, CoverCircles.hpp).
struct WallContact {
    glm::vec2 centre{0.0F, 0.0F}; ///< circle centre - bullet origin, world axes.
    float radius = 0.0F;
};

/// A circle shape is one SweepBlocked call (offset 0 == the legacy origin circle, bit-identical); an
/// oriented BOX goes through WorldCollision::SweepBoxBlocked (T-64): exact in the live GameScene world,
/// a covering row of circles (with registered slivers, CoverCircles.hpp) in circle-only worlds.
float SweepShapeBlocked(WorldCollision &world, glm::vec2 from, glm::vec2 to, const BulletShapeWorld &sw,
                        WallContact &hit) {
    if (sw.kind != BulletShape::Kind::Box) {
        hit = WallContact{sw.centreOffset, sw.radius};
        return world.SweepBlocked(from + sw.centreOffset, to + sw.centreOffset, sw.radius);
    }
    glm::vec2 off{0.0F, 0.0F};
    float r = 0.0F;
    const float t = world.SweepBoxBlocked(from, to, sw.centreOffset, sw.half, sw.boxAngle, off, r);
    hit = WallContact{off, r};
    return t;
}
} // namespace

void Simulation::IntegrateBullets() {
    for (BulletState &b : m_Bullets) {
        if (!b.active) {
            continue;
        }
        b.prevPos = b.pos;
        // P3.0c: Bullet01 / RGSBullet01 FixedUpdate: eulerAngles.z += rotate_angle once per fixed
        // step while awake, velocity untouched (the bullet keeps flying straight while it spins; the
        // oriented collider turns with it). The new heading holds for this whole step's tests.
        if (b.launch.spinDegPerStep != 0.0F) {
            b.angleRad += b.launch.spinDegPerStep * (3.14159265358979F / 180.0F);
        }
        b.pos += b.vel * kFixedStepSeconds;
        b.lifeMs -= kFixedStepMs;
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
        // T-57: swept test. The bullet centre moves b.prevPos -> b.pos (its FULL step path) while
        // the target hurtbox moves linearly from its step-start position to its current one;
        // BodyGeom::HurtSweepBeforeWall tests the RELATIVE segment against the hurtbox grown by
        // the bullet radius, returns the earliest hit time t on that same [0,1] step scale and
        // accepts it only if t <= tw, the wall contact fraction of the same full path (see the
        // T-57 block above IntegrateBullets; the wall is queried HERE, per bullet in id order, so
        // a box destroyed by an earlier bullet of this step no longer blocks later ones).
        // PORT CHOICE (T-57): a target is assumed to move LINEARLY (start -> end of step) inside
        // the step (enemies walk axis-separated: x then y, so the true path is an L; the
        // linear chord differs from it by less than one step of enemy travel, a few px).
        // Nothing else is approximated: circle hurtboxes use the exact segment-circle quadratic,
        // box hurtboxes the exact rounded box (Minkowski sum of box and bullet disc), and the
        // discrete end-of-step overlap is always a subset of what is reported. A target already
        // touching at the START of the step is reported at t = 0 (a bullet spawned inside a
        // hurtbox hits it; a piercing bullet that is still inside after a hit hits it again,
        // as the old end-of-step rule did while it stayed inside).
        // P3.0c: since this slice enemy / boss bullets carry their own prefab shape too (see the
        // shape block below); the 4 px kBulletRadius circle is only the fallback of a bullet with NO
        // shape (a sim without GameData, or a weapon whose bullet prefab is not in bullets.json).
        // P3.0c (PORT CHOICE T-64): a bullet that carries its prefab shape tests THAT shape --
        // a circle or an ORIENTED box at its prefab offset, turned with the bullet heading
        // (b.angleRad) -- against the hurtboxes (exact Minkowski sweep, BodyGeom::HurtSweepShape)
        // and, for walls, through WorldCollision::SweepBoxBlocked (live world = exact oriented-box
        // sweep; the covering circle row exists only as the fallback of circle-only test doubles). A bullet with no prefab shape (legacy / data missing) keeps the old
        // origin-centred circle of radiusPx (or the 4 px fallback), bit-identical to before.
        BulletShapeWorld sw;
        float bulletR = 0.0F;
        if (b.launch.shape.Present()) {
            sw = PlaceShape(b.launch.shape, b.angleRad);
            bulletR = b.launch.shape.InscribedRadius();
        } else {
            bulletR = b.radiusPx > 0.0F ? b.radiusPx : kBulletRadius;
            sw.kind = BulletShape::Kind::Circle;
            sw.radius = bulletR;
        }
        const glm::vec2 stepEnd = b.pos;
        float tw = -1.0F; // wall contact fraction of the full path; < 0 = free (or canThrough).
        WallContact wallHit{{0.0F, 0.0F}, bulletR};
        if (!b.canThrough) {
            tw = SweepShapeBlocked(*m_World, b.prevPos, stepEnd, sw, wallHit);
        }
        if (b.camp == 0) {
            // Player bullet -> enemies and boss; ONE target per bullet per step, the EARLIEST
            // hit time wins (ties: lowest enemy index, then the boss -- the old scan order).
            // The enemy gate tests the `dead` FLAG (set later in Cull), not stats.IsDead():
            // a second bullet landing the same step as a lethal one is consumed on the
            // about-to-die enemy. Intentional + deterministic -- all in-flight bullets land.
            bool consumed = false;
            std::size_t bestEi = m_Enemies.size(); // == size: no enemy hit.
            float bestT = 2.0F;
            for (std::size_t ei = 0; ei < m_Enemies.size(); ++ei) {
                auto &e = m_Enemies[ei];
                if (e->State().dead) {
                    continue;
                }
                const glm::vec2 from = ei < m_EnemyStepStart.size() ? m_EnemyStepStart[ei]
                                                                    : e->State().pos;
                float t = 0.0F;
                // The bullet circle hits the target's HURTBOX (prefab root trigger collider).
                if (e->State().geom.HurtSweepShapeBeforeWall(from, e->State().pos, b.prevPos, stepEnd,
                                                             sw, tw, t) &&
                    t < bestT) {
                    bestT = t;
                    bestEi = ei;
                }
            }
            bool bossFirst = false;
            if (m_Boss != nullptr && !m_Boss->State().dead) {
                float t = 0.0F;
                if (m_Boss->State().geom.HurtSweepShapeBeforeWall(m_BossStepStart, m_Boss->State().pos,
                                                                  b.prevPos, stepEnd, sw, tw, t) &&
                    t < bestT) {
                    bestT = t;
                    bossFirst = true;
                }
            }
            if (bestEi < m_Enemies.size() && !bossFirst) {
                const std::size_t ei = bestEi;
                auto &e = m_Enemies[ei];
                Game::Combat::AttackerInput in;
                in.baseDamage = b.damage;
                in.critical = b.critical;
                in.repelInputMagnitude = b.repel * kRepelScale;
                const Game::Combat::HitResult hr =
                    Game::Combat::ResolveHit(in, m_HitRng, Game::Combat::Defender::ENEMY);
                Game::Combat::ApplyToEnemy(e->MutableState().stats, hr, true);
                // item 2 (R19): DeathSource, not a plain bool -- see EntityState.hpp.
                e->MutableState().SetDeathSource(DeathSource::PlayerBullet);
                if (!e->State().kinematic) {
                    e->ApplyForce(Normalize(b.vel), hr.repelMagnitude);
                }
                m_Events.push_back(SimEvent{SimEventType::AnimTrigger,
                                            static_cast<std::uint32_t>(ei), "hurt"}); // A
                RollBulletBuff(b, static_cast<std::uint32_t>(ei), hr.isCrit); // P3.2
                // P3.3.b: C10/C13's every-3rd-qualifying-hit armor-regen passive; a
                // no-op (returns 0) for every other hero.
                if (m_Skill != nullptr) {
                    const int armorGain = m_Skill->OnPlayerBulletHit();
                    if (armorGain > 0) {
                        m_PlayerStats.RestoreArmor(armorGain);
                    }
                }
                consumed = true;
            }
            if (bossFirst) {
                Game::Combat::AttackerInput in;
                in.baseDamage = b.damage;
                in.critical = b.critical;
                in.repelInputMagnitude = b.repel * kRepelScale;
                const Game::Combat::HitResult hr =
                    Game::Combat::ResolveHit(in, m_HitRng, Game::Combat::Defender::ENEMY);
                Game::Combat::ApplyToEnemy(m_Boss->MutableState().stats, hr, true);
                m_Boss->MutableState().SetDeathSource(DeathSource::PlayerBullet); // item 2 (R19)
                m_Boss->OnHurt(m_Boss->State().stats.hp, m_Boss->State().stats.maxHp);
                m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kBossViewId, "hurt"}); // A
                RollBulletBuff(b, kBossViewId, hr.isCrit); // P3.2
                if (m_Skill != nullptr) {
                    const int armorGain = m_Skill->OnPlayerBulletHit(); // P3.3.b
                    if (armorGain > 0) {
                        m_PlayerStats.RestoreArmor(armorGain);
                    }
                }
                consumed = true;
            }
            if (consumed && b.pierce <= 0) {
                b.active = false;
                m_Impacts.push_back(BulletImpact{b.launch.bulletIdx, b.camp, false, b.pos, b.angleRad});
            } else if (consumed) {
                --b.pierce; // canThrough/pierce: survive and keep going.
            }
        } else {
            // Enemy bullet -> player (swept against the player's own step motion; a jump larger
            // than kMaxPlayerSweepPx is a teleport/dash, not motion: treated as stationary).
            glm::vec2 playerFrom = m_Input.playerPos;
            if (m_PlayerPrevValid) {
                const glm::vec2 pd = m_Input.playerPos - m_PlayerPrevPos;
                if (pd.x * pd.x + pd.y * pd.y <= kMaxPlayerSweepPx * kMaxPlayerSweepPx) {
                    playerFrom = m_PlayerPrevPos;
                }
            }
            float hitT = 0.0F; // hit time unused: one target, so no ordering needed.
            if (m_Input.playerAlive &&
                m_PlayerGeom.HurtSweepShapeBeforeWall(playerFrom, m_Input.playerPos, b.prevPos,
                                                      stepEnd, sw, tw, hitT)) {
                Game::Combat::AttackerInput in;
                in.baseDamage = b.damage;
                in.critical = b.critical;
                in.repelInputMagnitude = b.repel * kRepelScale;
                const Game::Combat::HitResult hr =
                    Game::Combat::ResolveHit(in, m_HitRng, Game::Combat::Defender::PLAYER);
                ApplyDamageToPlayer(hr); // P3.3.b: C08's shield can absorb this hit.
                m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kPlayerViewId, "hurt"}); // A
                // P3.2: enemy bullets never carry a buff today (EnemyGunDef has no
                // bullet-prefab link into bullets.json's buff-carrying rows -- NOT
                // PORTED, see the P3.2 report); this call is a no-op (b.hasBuff is
                // always false here) kept for symmetry / future wiring. Were one
                // ever wired, camp != 0 here, so the decompile's zero-probability
                // branch would APPLY it (see ShouldApplyBulletBuff).
                RollBulletBuff(b, kPlayerViewId, hr.isCrit);
                b.active = false;
                m_Impacts.push_back(BulletImpact{b.launch.bulletIdx, b.camp, false, b.pos, b.angleRad});
            }
        }
        // T-57: a bullet that survived its target test (missed, or pierced) and whose path hit a
        // wall is consumed at the contact point. A target struck at t <= tw already won above
        // (target-first tie rule); a target only reachable after the wall was never accepted.
        if (b.active && tw >= 0.0F) {
            if (tw < 1.0F) {
                b.pos = b.prevPos + (stepEnd - b.prevPos) * tw; // the first contact point.
            }
            b.active = false;
            m_Impacts.push_back(BulletImpact{b.launch.bulletIdx, b.camp, true, b.pos, b.angleRad});
            // Phase 3: a consumed bullet damages a destructible design-room box at
            // the impact point (no-op for walls / under NullWorldCollision, so the
            // combat hash is unchanged). Scene-side mutation; sim RNG untouched.
            // P3.0c (T-64): at the contact probe (exact world: radius-1 circle on the blocker; circle-only
            // fallback: the covering-row circle that touched); its centre rides on the bullet origin.
            m_World->DamageObstacle(b.pos + wallHit.centre, wallHit.radius);
        }
    }
}
namespace {
// item 2 (R19): the ONLY DeathSource values that attribute a death to the player
// -> ICharSkill::OnEnemyKilled (C04's kill-combo). None (never hit), Other
// (non-player DoT), and Forced (KillAllHostiles/SK_FORCE_KILL, immutable) never do.
bool IsPlayerDeathSource(DeathSource src) {
    return src == DeathSource::PlayerBullet || src == DeathSource::PlayerCut ||
           src == DeathSource::PlayerDot || src == DeathSource::PlayerSkill;
}
} // namespace

void Simulation::Cull() {
    for (std::size_t ei = 0; ei < m_Enemies.size(); ++ei) {
        auto &e = m_Enemies[ei];
        if (!e->State().dead && e->State().stats.IsDead()) {
            const bool byPlayer = IsPlayerDeathSource(e->State().deathSource); // read before Kill().
            m_LastDeathSource = e->State().deathSource;
            if (byPlayer) {
                ++m_PlayerKillCount;
            }
            e->Kill(); // cancels its scheduler cadence; kept in m_Enemies for stable view id.
            m_Events.push_back(SimEvent{SimEventType::AnimTrigger,
                                        static_cast<std::uint32_t>(ei), "death"}); // A
            if (byPlayer && m_Skill != nullptr) {
                m_Skill->OnEnemyKilled(); // P3.3.b: C04's kill-combo; no-op for the rest.
            }
        }
    }
    if (m_Boss != nullptr && !m_Boss->State().dead && m_Boss->State().stats.IsDead()) {
        const bool byPlayer = IsPlayerDeathSource(m_Boss->State().deathSource);
        m_LastDeathSource = m_Boss->State().deathSource;
        if (byPlayer) {
            ++m_PlayerKillCount;
        }
        m_Boss->Kill();
        m_Events.push_back(SimEvent{SimEventType::AnimTrigger, kBossViewId, "death"}); // A
        if (byPlayer && m_Skill != nullptr) {
            m_Skill->OnEnemyKilled();
        }
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
    m_PlayerPrevPos = m_Input.playerPos; // T-57: next step's player sweep origin.
    m_PlayerPrevValid = true;
}

int Simulation::Advance(float dtMs, const WorldInputs &in) {
    // R19/item-1 fix: detect the skill press-edge HERE, at Advance()-call
    // granularity, BEFORE m_Input is overwritten below and BEFORE we know whether
    // this call will run any fixed Step()s at all. m_Clock.Advance(dtMs) can return
    // 0 for a short dt (its accumulator has not yet crossed kFixedStepMs), in which
    // case Step()/TickSkill() never run this call -- if edge detection lived only in
    // TickSkill() (as it did pre-R19), a press arriving on such a zero-step call
    // would be silently lost the moment the NEXT Advance() overwrites m_Input with a
    // different level, since no Step() ever got to see skill=true. Latching the edge
    // into m_SkillPressPending instead means it survives until a fixed Step() (any
    // later Advance() call, however many it takes to cross the next kFixedStepMs
    // boundary) actually consumes it in TickSkill().
    if (in.skill && !m_SkillHeldPrev) {
        m_SkillPressPending = true;
    }
    m_SkillHeldPrev = in.skill;
    m_Input = in;
    m_PlayerShotsLastAdvance = 0;
    m_PlayerEnergyGrantedLastAdvance = 0;
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
    // COLLIDER DATA: body circle + hurtbox from the species prefab (placeholder
    // kEnemyBodyRadius only for a def with no collider data, e.g. a bare EnemyDef{}).
    ec->MutableState().geom = MakeBodyGeom(def.body, def.hurtbox, kEnemyBodyRadius);
    // ENEMY RE-KEY: FAITHFUL @ enemies.csv -- seed the real per-species maxHp when
    // the def carries one; kSliceEnemyHp is only the fallback for a def with no
    // species data (e.g. a default-constructed test EnemyDef{}), so old tests that
    // pass an empty EnemyDef{} keep their old flat-HP behaviour unchanged.
    const int seedHp = def.maxHp > 0 ? def.maxHp : kSliceEnemyHp;
    ec->MutableState().stats.hp = seedHp;
    ec->MutableState().stats.maxHp = seedHp;
    ec->SetBulletLaunch(LaunchFor(def)); // P3.0c: its own bullet prefab (sprite / shape / spin)
    ec->Activate(m_Scheduler, m_FireIntents);        // asleep until WakeByRoom().
    m_Enemies.push_back(std::move(ec));
}
void Simulation::SetBoss(float baseShootCd, glm::vec2 spawn, int maxHp, int roomId, int seed,
                         const std::string &bossId, const Game::EnemyDef *def) {
    // (d) dispatch: BrainFactory routes bossId -> its brain adapter. The default
    // "BossAI01" keeps the legacy path byte-identical.
    m_Boss = BrainFactory::MakeBossPtr(bossId, baseShootCd, spawn, maxHp, seed);
    m_Boss->MutableState().roomId = roomId;
    m_Boss->MutableState().geom =
        def != nullptr ? MakeBodyGeom(def->body, def->hurtbox, kBossBodyRadius)
                       : BodyGeom::Fallback(kBossBodyRadius);
    if (def != nullptr) {
        m_Boss->SetBulletLaunch(LaunchFor(*def)); // P3.0c (T-65): the boss's primary bullet
    }
    m_Boss->Activate(m_Scheduler, m_FireIntents); // asleep until WakeByRoom().
}
void Simulation::EquipWeapon(const Game::WeaponDef &def, const std::string &weaponId,
                             int seed, const Game::GameData *data) {
    m_Weapon.emplace(BrainFactory::MakeWeapon(def, weaponId, seed, data)); // cold rebuild
}
void Simulation::SetPlayerSkill(const std::string &charId, float skillCd, float inSkillTime,
                                int atk, const Game::CharacterDef *def) {
    // B1-P4a/P3.3.b/c (d) dispatch: charId -> per-hero ICharSkill adapter. C01/C02/
    // C03/C04/C05/C07/C08/C10/C11/C12/C13 have real bodies; C06/C09 stay gate+cooldown
    // stubs. The skill starts READY (each brain ctor). Single owned member, parallel
    // to m_Weapon. m_PlayerAtk feeds RoleAtkMelee (C04's cut damage = atk + 3/7);
    // @p atk also seeds C12's RGHand.AtkCut (role_attribute.atk) and C03's none.
    m_Skill = MakeCharSkill(charId, skillCd, inSkillTime, def, atk);
    m_PlayerAtk = atk;
}

void Simulation::KillAllHostiles() {
    for (auto &e : m_Enemies) {
        if (!e->State().dead) {
            e->MutableState().stats.hp = 0;
            // item 2 (R19 fix, was item 5's plain lastHitByPlayer=false clear): a
            // forced clear is NOT a player kill -- and unlike the old bool, this
            // assignment LOCKS the attribution. KillAllHostiles zeroes hp but does
            // not mark the entity `dead` (only Cull() does, the next time it runs),
            // so between now and that Cull() a player bullet/cut/DoT tick could
            // still land on this (not-yet-culled) entity; direct-assigning Forced
            // here (bypassing SetDeathSource's own Forced-is-immutable guard, which
            // only protects FUTURE writes) means every one of those later writes
            // goes through SetDeathSource and is silently ignored, so Cull() still
            // sees Forced and never fires ICharSkill::OnEnemyKilled for a kill the
            // player never made.
            e->MutableState().deathSource = DeathSource::Forced;
            e->MutableState().deathLocked = true; // item 2 (R20): belt-and-suspenders --
            // the `deathSource == Forced` guard in SetDeathSource already rejects every
            // later write unconditionally, but locking here too keeps the invariant
            // "Forced is immutable" true of BOTH fields, not just one of them.
        }
    }
    if (m_Boss != nullptr && !m_Boss->State().dead) {
        m_Boss->MutableState().stats.hp = 0;
        m_Boss->MutableState().deathSource = DeathSource::Forced; // item 2 (R19), see above.
        m_Boss->MutableState().deathLocked = true;                // item 2 (R20), see above.
    }
}

} // namespace Game::Sim
