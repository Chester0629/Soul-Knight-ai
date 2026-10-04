#ifndef GAME_CHAR_SKILL_C07_HPP
#define GAME_CHAR_SKILL_C07_HPP

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "combat/SkillWorld.hpp"
#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BatSwarm
 * @brief The BulletBat flock C07's RoleSkill releases (engine-free), ported from the
 *        BulletBat state machine (DISASM 0xaf290c Start / 0xaf2a8c FixedUpdate /
 *        0xaf37a0 FindTarget; T-39). All lengths are Unity world units, speeds units/s.
 *
 * Per bat, per fixed step: Invoke("FindTarget", 0.5s) fires once; then the state
 * machine runs (0 = free flight, 1 = homing turn, 3 = dash, 2 = return) and the
 * Rigidbody2D integrates position += velocity * dt.
 *
 *  - Start (DISASM 0xaf2950-0xaf2a54): velocity = speed * scale.x * (cosf(a), sinf(a))
 *    with a = move_angle taken AS RADIANS (cosf/sinf PLT stubs 0x1cef30/0x1ceab4
 *    resolved via .rel.plt) -- a decompiled quirk kept verbatim; scale.x = -1 for the
 *    first half of the flock, +1 for the rest. Invoke(FindTarget, 0.5f) (mov r2,
 *    #0x3f000000 @0xaf2a54).
 *  - FindTarget (DISASM 0xaf37a0): CircleCastAll(bat position, radius 12.0 [movt 0x4140
 *    @0xaf38e4], layer "Body_E"); nearest hit (best seeded 100.0) -> target, state 1;
 *    none -> target = the player (source_object), state 2.
 *  - State 1 (DISASM 0xaf2b1c-0xaf35f4): target hp <= 0 -> FindTarget again; else
 *    angle = Vector2.Angle(dir to target, velocity dir) (UNSIGNED degrees); if
 *    |angle| > 5 [pool 0xaf2d58] move_angle += angle * 0.1 [pool 0xaf2d5c], else
 *    state = 3; velocity = speed * (cos, sin)(move_angle * pi / 180) (degrees here, the
 *    mixed radians/degrees convention is the original's); if the target is < 1.0 away
 *    [pool 0xaf3770] and !has_hit the bat hits.
 *  - State 3 (DISASM 0xaf2d60-0xaf2f98): velocity = normalize(target - bat) * speed * 3
 *    [movt 0x4040 @0xaf2e60]; target < 0.5 away [pool 0xaf317c] and !has_hit -> hit.
 *  - Hit (DISASM 0xaf2f9c-0xaf3718): target.GetHurt(damage) (vcall slot 12 of
 *    RGEController @0xaf2ff0), knockback GetForce(velocity, 9.0) [movt 0x4110] and a
 *    hit effect (NOT PORTED, T-45), has_hit = true, state = 2, target = the player.
 *  - State 2 (DISASM 0xaf3180-0xaf3454): velocity = normalize(player - bat) * speed * 3;
 *    player < 1.0 away [pool 0xaf3770] -> C07Controller.BatBack(has_hit) and Destroy.
 */
class BatSwarm {
public:
    /// BulletBat ctor default speed (TYPED game_typed.c:89494-89500: speed = 6.0; the
    /// prefab does not override it).
    static constexpr float kSpeedUnitsPerSec = 6.0F;
    static constexpr float kFindTargetDelaySec = 0.5F;
    static constexpr float kFindRadiusUnits = 12.0F;
    static constexpr float kBestDistInit = 100.0F;
    static constexpr float kSteerGain = 0.1F;
    static constexpr float kAimToleranceDeg = 5.0F;
    static constexpr float kDashSpeedMul = 3.0F;
    static constexpr float kHitRangeHoming = 1.0F;
    static constexpr float kHitRangeDash = 0.5F;
    static constexpr float kArriveRange = 1.0F;

    struct Hit {
        std::uint32_t id = 0;
        int damage = 0;
    };

    /// RoleSkill's spawn loop (DISASM 0x266b28-0x266d2c): @p count = 2n bats, bat k has
    /// move_angle = (-180*n + 180*k) / n (C integer division), mirrored (scale.x -1)
    /// for k < n; all start at @p origin (the player). @p damage = BulletBat.damage
    /// (prefab data), @p speed = BulletBat.speed.
    void Launch(int count, glm::vec2 origin, int damage, float speed);

    /// One fixed step. Appends this step's skill hits (@p hits) and, for every bat that
    /// reached the player, its has_hit flag (@p backs, in arrival order).
    void Step(float dtSec, glm::vec2 playerPos, const std::vector<SkillTarget> &hostiles,
              std::vector<Hit> &hits, std::vector<bool> &backs);

    std::size_t Alive() const { return m_Bats.size(); }

    /// Test/probe: state / position of bat @p i (0 = free flight, 1 = homing, 2 = return,
    /// 3 = dash).
    int StateOf(std::size_t i) const { return m_Bats[i].state; }
    glm::vec2 PosOf(std::size_t i) const { return m_Bats[i].pos; }
    glm::vec2 VelOf(std::size_t i) const { return m_Bats[i].vel; }

private:
    static constexpr std::uint32_t kPlayerTarget = 0xFFFFFFFFU;
    struct Bat {
        glm::vec2 pos{0.0F, 0.0F};
        glm::vec2 vel{0.0F, 0.0F};
        float moveAngle = 0.0F;
        int state = 0;
        std::uint32_t target = kPlayerTarget;
        bool hasHit = false;
        bool findFired = false;
        float findTimer = 0.0F;
        int damage = 0;
        float speed = 0.0F;
    };
    void FindTarget(Bat &b, const std::vector<SkillTarget> &hostiles) const;
    std::vector<Bat> m_Bats;
};

/**
 * @class CharSkillC07
 * @brief Skill brain for hero C07 (the Vampire: C07Controller, an RGController player).
 *
 * AUDIT (source -> status). TYPED = game_typed.c lines visible in the cited range
 * (FAITHFUL). DISASM = libil2cpp.so ARM disassembly past the typed truncation (T-39).
 *
 *  - ctor (TYPED 1881-1895): base RGController ctor only; bat_count / bat_hit_count /
 *    reduce_skill_cd default 0. FAITHFUL.
 *  - Update (TYPED 1937-1946): awake -> AttributeUpdate (SkillReload cooldown count-up,
 *    even in_skill) + SeachUpdate. No in_skill_time countdown. FAITHFUL.
 *  - RoleSkill gate (TYPED 1969-1997): awake && skill_ready && !in_skill. FAITHFUL.
 *    Past the gate, DISASM 0x26699c-0x266d60 (the orphan tail game_full.c:156791-156831
 *    shows the same stores): BuffStatue.UseSkill hook; bat_particle.Play (presentation,
 *    T-51); bat_count = 0 (+0x98) ; bat_hit_count = 0 (+0x9c) -- reduce_skill_cd (+0xa0)
 *    is NOT reset ; in_skill = 1 ; n = skill_strengthen ? 5 : 3 ; the loop releases 2n
 *    BulletBat clones (6, or 10 strengthened -- exactly the BatBack end threshold) at
 *    the player (BatSwarm::Launch) ; PlayEffect(clip_skill). NO RoleSkillEnd call: the
 *    skill stays in_skill until the last bat is back.
 *  - BatBack(has_hit) (TYPED 2058-2118 shows the prologue and the miss branch; the
 *    has_hit branch is DISASM 0x2670ec-0x267264 where the typed body truncates into the
 *    Singleton tail call):
 *      bat_count++ (TYPED 2073) ;
 *      has_hit: bat_hit_count++ ; at bat_hit_count >= (RGGameProcess.buff_vampire_hp
 *        ? 2 : 3) [DISASM 0x267148-0x26715c] -> RestoreHealth(1) when hp < max_hp, else
 *        RestoreEnergy(5) [0x267160-0x267258], bat_hit_count = 0 [0x267264] ;
 *      !has_hit: reduce_skill_cd += skill_cd / (skill_strengthen ? 10.0 : 6.0)
 *        (TYPED 2085-2097, reached only on the !has_hit path per the DISASM branch `bne 0x267178`
 *        @0x2670ec; the two divisors are the literal-pool floats DAT_002672d4 =
 *        6.0 / DAT_002672d8 = 10.0, read from libil2cpp.so, Ghidra 0x2672d4/0x2672d8 = file 0x2572d4/0x2572d8, `tools/arm_disasm.py 0x2672d4 0x2672dc --floats`) --
 *        NOTE the cooldown refund is on the MISS path, not the hit path (TYPED+DISASM) ;
 *      bat_count == (strengthen ? 10 : 6) -> RoleSkillEnd (TYPED 2098-2113 and DISASM
 *        0x2672a4-0x2672bc, vcall slot 25).
 *  - RoleSkillEnd (TYPED 2004-2026): in_skill = false ; ReSetSkillReload (this_skill_time
 *    = 0) ; SkillReload(reduce_skill_cd) [the float arg is the reduce_skill_cd read, the
 *    decompiler printed it in the MethodInfo slot] ; reduce_skill_cd = 0 ;
 *    UpdateShadowLock (tint, T-51). FAITHFUL.
 *  - KillSomeOne (TYPED 2034-2050 shows only the prologue + a Singleton tail call;
 *    DISASM 0x266e08-0x26706c): if RGGameProcess.buff_vampire_hp && hp < max_hp:
 *    Random.Range(0,100) <= 7 -> RestoreHealth(1) ; if buff_energy: RestoreEnergy(2) ;
 *    RGGameProcess.kill_count++ (run bookkeeping, already GameScene::RecordKill). The two
 *    run-buff flags are not wired (T-42), so the sim's KillSomeOne is a no-op.
 *  - NOT PORTED: the bats' knockback / hit-effect prefab / sound / achievement ping
 *    (T-45); the BuffStatue / shadow / particle presentation (T-51).
 *
 * The brain draws RNG ONLY inside KillSomeOne's buffed branch (Random.Range -> own
 * RGRandom stream, never taken while the run buffs are unwired).
 */
class CharSkillC07 {
public:
    /// n of the RoleSkill loop: 3 (6 bats) or 5 (10 bats strengthened). DISASM 0x266ae4.
    static constexpr int kPairsNormal = 3;
    static constexpr int kPairsStrengthened = 5;
    /// BatBack's cooldown-refund divisors (literal pool 0x2672d4 / 0x2672d8).
    static constexpr float kRefundDivisorNormal = 6.0F;
    static constexpr float kRefundDivisorStrengthened = 10.0F;
    /// has_hit heal cadence: every 3rd hit (2nd with buff_vampire_hp). DISASM 0x267150.
    static constexpr int kHitsPerHeal = 3;
    static constexpr int kHitsPerHealBuffed = 2;
    static constexpr int kHealAmount = 1;       ///< RestoreHealth(1) @0x267238.
    static constexpr int kEnergyRestore = 5;    ///< RestoreEnergy(5) @0x26724c.
    static constexpr int kKillHealRollMax = 7;  ///< Random.Range(0,100) <= 7 @0x266ee0.
    static constexpr int kKillEnergyRestore = 2;///< RestoreEnergy(2) @0x266fec.

    /// @param skillCd cooldown length, seconds (clamped >= 0). Starts READY.
    explicit CharSkillC07(float skillCd);

    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// T-42: RGGameProcess run-buff flags + skill_strengthen (RoleAttributePlayer
    /// .get_skill_strengthen == c_level > 4, DISASM 0x52612c) are not wired in the sim;
    /// these setters exist so the brain is complete and testable.
    void SetRunBuffs(bool vampireHp, bool energy) {
        m_BuffVampireHp = vampireHp;
        m_BuffEnergy = energy;
    }
    void SetSkillStrengthen(bool v) { m_Strengthen = v; }

    /// RoleSkill (caller checks awake). @return true when the ultimate was entered
    /// (in_skill = 1; counters reset); the flock is then taken with ConsumeLaunch().
    bool TryActivateSkill();

    /// The 2n BulletBat clones RoleSkill released (0 when none pending). One-shot.
    int ConsumeLaunch();

    struct BatBackResult {
        int healHp = 0;          ///< RestoreHealth amount (0 or 1).
        int restoreEnergy = 0;   ///< RestoreEnergy amount (0 or 5).
        bool skillEnded = false; ///< this bat was the last: RoleSkillEnd ran.
    };
    /// C07Controller.BatBack(has_hit). @p hp / @p maxHp are the player's current vitals.
    BatBackResult BatBack(bool hasHit, int hp, int maxHp);

    struct KillResult {
        int healHp = 0;
        int restoreEnergy = 0;
    };
    /// C07Controller.KillSomeOne (DISASM 0x266e08). Draws Random.Range(0,100) only when
    /// buff_vampire_hp && hp < maxHp.
    KillResult KillSomeOne(int hp, int maxHp);

    /// RoleSkillEnd (TYPED 2004-2026): in_skill = false ; this_skill_time = 0 ;
    /// SkillReload(reduce_skill_cd) ; reduce_skill_cd = 0.
    void EndSkill();

    /// One awake frame: SkillReload only (TYPED 1937-1946).
    void Tick(float dtMs);

    bool InSkill() const { return m_InSkill; }
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }
    float CooldownRemaining() const;
    float SkillCd() const { return m_SkillCd; }
    int BatCount() const { return m_BatCount; }
    int BatHitCount() const { return m_BatHitCount; }
    float ReduceSkillCd() const { return m_ReduceSkillCd; }
    float ThisSkillTime() const { return m_ThisSkillTime; }
    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    float m_SkillCd = 0.0F;
    float m_ThisSkillTime = 0.0F;
    bool m_InSkill = false;
    bool m_Strengthen = false;
    bool m_BuffVampireHp = false;
    bool m_BuffEnergy = false;
    int m_BatCount = 0;        ///< bat_count (+0x98).
    int m_BatHitCount = 0;     ///< bat_hit_count (+0x9c).
    float m_ReduceSkillCd = 0.0F; ///< reduce_skill_cd (+0xa0).
    int m_LaunchPending = 0;
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C07_HPP */
