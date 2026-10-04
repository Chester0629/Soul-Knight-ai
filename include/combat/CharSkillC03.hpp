#ifndef GAME_CHAR_SKILL_C03_HPP
#define GAME_CHAR_SKILL_C03_HPP

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "combat/SkillWorld.hpp"
#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class ThunderChain
 * @brief The BulletThunder chain-lightning object C03's CreateTunder spawns
 *        (engine-free). StartThunder strikes the first target; every
 *        kHopIntervalSec FindNextTarget either arcs to the nearest not-yet-hit
 *        hostile within kRadiusUnits of the strike point or ends the chain.
 *
 * Evidence legend (see CharSkillC03 for the full audit): DISASM = ARM disassembly
 * of 1.7.10/lib/armeabi-v7a/libil2cpp.so (file offset = Ghidra address - 0x10000),
 * recovered where game_typed.c truncates the body ("Subroutine does not return");
 * reproducible with tools/arm_disasm.py. T-39 (evidence class).
 */
class ThunderChain {
public:
    /// FindNextTarget re-arms itself with Invoke(..., 0.15f): StartThunder
    /// (DISASM 0xb00318-0xb00344, movw 0x999a/movt 0x3e19 = 0.15f) and the hop tail
    /// (DISASM 0xaffdb4-0xaffdcc). Unity Invoke re-arms from the firing frame, so the
    /// timer restarts at the full interval after each hop (8 fixed steps of 20 ms).
    static constexpr float kHopIntervalSec = 0.15F;
    /// CircleCastAll radius around the thunder object (DISASM 0xaff054, movt 0x4120).
    static constexpr float kRadiusUnits = 10.0F;
    /// A hop is taken while the OLD hop count is <= 3 (DISASM 0xaff3e0-0xaff3e8,
    /// `cmp #3; ble`), so at most 4 hops follow the first strike.
    static constexpr int kCountGate = 3;
    /// "best distance so far" seed of the nearest-target scan (DISASM pool 0xaff3d0 =
    /// 0x42c80000 = 100.0f).
    static constexpr float kBestDistInit = 100.0F;

    struct Hit {
        std::uint32_t id = 0;
        int damage = 0;
    };

    /// StartThunder(intensify, first, atk): records the first strike point (the
    /// thunder object's fixed position -- the hop search circle stays centred on it,
    /// DISASM: nothing in FindNextTarget moves the thunder transform), seeds the
    /// has_hit_list with @p first and arms the 0.15 s timer. The first strike's own
    /// damage (GetHurt(atk)) is the CALLER's (the adapter emits it) -- DISASM 0xb00048.
    /// @p intensify is skill_strengthen (T-42: not wired, always false).
    void Start(const SkillTarget &first, int atk, bool intensify);

    /// Advance one fixed step. Appends this step's hits. @return false once finished.
    bool Step(float dtSec, const std::vector<SkillTarget> &hostiles, std::vector<Hit> &hits);

    bool Done() const { return m_Done; }
    int HopCount() const { return m_Count; }
    std::size_t HitListSize() const { return m_HitList.size(); }

private:
    bool m_Done = true;
    bool m_Intensify = false;
    int m_Atk = 0;
    int m_Count = 0;            ///< BulletThunder.count (+0x20).
    float m_Timer = 0.0F;       ///< seconds until the next FindNextTarget.
    glm::vec2 m_Origin{0.0F, 0.0F};
    SkillTarget m_Last{};       ///< last_target (+0x28).
    std::vector<std::uint32_t> m_HitList; ///< has_hit_list (+0x2c).
};

/**
 * @class CharSkillC03
 * @brief Skill brain for hero C03 (the Wizard: C03Controller, an RGController player).
 *
 * AUDIT (every claim: source -> status). TYPED = game_typed.c lines visible in the
 * cited range (FAITHFUL). DISASM = libil2cpp.so ARM disassembly past the typed
 * truncation (T-39). INFERRED / PORT CHOICE / NOT PORTED carry their own T-ID.
 *
 *  - Update (TYPED 841-850): awake -> AttributeUpdate (SkillReload: this_skill_time
 *    counts up to skill_cd EVERY awake frame, even in_skill) + SeachUpdate (owner).
 *    No in_skill_time countdown -> Tick() advances ONLY the cooldown. FAITHFUL.
 *  - RoleSkill gate (TYPED 872-901): awake && skill_ready && !in_skill. FAITHFUL.
 *    The rest of RoleSkill is the truncated get_transform tail; DISASM 0x264264-0x2644e8:
 *      * BuffStatue.UseSkill (Find "buff_statue") -- presentation hook, already surfaced
 *        by Simulation as the "skill_use" event -> GameScene's statue logic.
 *      * ResourcesUtil.Load("effect_c3_skill") aura child + Animator.SetTrigger("skill")
 *        + PlayEffect(clip_skill) -- presentation, NOT PORTED (T-51).
 *      * in_skill = 1 (strb [this,#0x55] @0x26446c).
 *      * MonoBehaviour.Invoke("CreateTunder", 0.4f) (@0x264498 string literal
 *        "CreateTunder"; movw 0xcccd/movt 0x3ecc = 0.4f @0x26449c) -> kCreateTunderDelaySec.
 *  - CreateTunder (TYPED 908-924, truncated; DISASM 0x26451c-0x264c14):
 *      * CircleCastAll(player pos, radius 14.0 [movt 0x4160 @0x264674], zero dir,
 *        zero distance, layer "Body_E"), then the NEAREST hit (Vector2.Distance to the
 *        player, best seeded 100.0 [pool 0x264a00]) whose Linecast on layer "Wall"
 *        from (hit.x, hit.y+0.5) to (player.x, player.y+0.5) [pool 0x264a04 = 0.5f]
 *        is NOT blocked. PickNearestTarget.
 *      * No target -> nothing spawns, but RoleSkillEnd still runs (the cooldown is
 *        spent regardless).
 *      * Target found and RGGameSceneManager.game_state == 1 (always true in the sim):
 *        camera shake (presentation, T-51) + Instantiate(thunder) at the target +
 *        BulletThunder.StartThunder(skill_strengthen, target, 8) (@0x264ba0: r3 = #8)
 *        -> atk = 8 (stored @0xaffee4, GetHurt(atk) vcall @0xb00048) = kStrikeDamage.
 *      * Tail: vcall slot 25 (RoleSkillEnd) @0x264bf4 -- unconditionally.
 *  - RoleSkillEnd (TYPED 924-936, truncated; DISASM 0x264c50-0x264ce4): hand
 *    GameObject.SetActive(true) (presentation) ; in_skill = 0 ; ReSetSkillReload
 *    (this_skill_time = 0) ; UpdateShadowLock (tint, T-51).
 *  - DeadChild (TYPED 943-956): in_skill = false; this_skill_time = 0. FAITHFUL
 *    (exposed as DeadChild(); the sim has no hero-death/revive hook -> T-50).
 *  - Thunder chain (BulletThunder.StartThunder/FindNextTarget, DISASM 0xaffe6c /
 *    0xafef08): see ThunderChain. NOT PORTED (T-43): the dizzy_rate roll (0x46 = 70,
 *    +20 intensified; Random.Range(0,100) < dizzy_rate applies the BuffElectric
 *    prefab buff_ele, @0xb000d4-0xb0023c) -- the sim has no per-enemy buff-prefab
 *    hook for a skill hit; thunder_child/aura visuals; the buff_reflection run-buff
 *    count-- (T-42).
 *
 * NO RGRandom draw anywhere in the ported bodies (the electric roll that would draw is
 * NOT PORTED), so the Rng member is parity-only, as for the other heroes.
 */
class CharSkillC03 {
public:
    /// MonoBehaviour.Invoke("CreateTunder", 0.4f). DISASM 0x26449c-0x2644b4.
    static constexpr float kCreateTunderDelaySec = 0.4F;
    /// CircleCastAll radius of the first target scan. DISASM 0x264674 (movt 0x4160).
    static constexpr float kSearchRadiusUnits = 14.0F;
    /// Nearest-scan seed (DISASM pool 0x264a00 = 100.0f) and the Linecast y offset
    /// (DISASM pool 0x264a04 = 0.5f).
    static constexpr float kSearchBestInit = 100.0F;
    static constexpr float kLosOffsetY = 0.5F;
    /// BulletThunder.StartThunder's value3 -> atk. DISASM 0x264ba0 (mov r3,#8).
    static constexpr int kStrikeDamage = 8;

    /// @param skillCd cooldown length, seconds (RoleAttributePlayer.skill_cd; clamped >= 0).
    /// Starts READY (this_skill_time == skill_cd) -- the PlayerDash/C01 convention.
    explicit CharSkillC03(float skillCd);

    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// RoleSkill (the awake check is the caller's): gate skill_ready && !in_skill, then
    /// in_skill = 1 and the 0.4 s Invoke is armed (DISASM). @return true when entered.
    bool TryActivateSkill();

    /// True once the armed Invoke("CreateTunder") has elapsed and not yet been run.
    bool CreateTunderDue() const { return m_InSkill && m_TimerArmed && m_InvokeLeftMs <= 0.0F; }

    struct TunderDecision {
        bool strike = false; ///< a thunder object spawns (target found && game_state == 1).
        int damage = 0;      ///< StartThunder atk (kStrikeDamage) when strike.
    };
    /// CreateTunder: @p targetFound is the nearest-target scan result. Consumes the
    /// due Invoke, then runs RoleSkillEnd UNCONDITIONALLY (DISASM 0x264bf4).
    TunderDecision CreateTunder(bool targetFound);

    /// RoleSkillEnd: in_skill = 0 ; this_skill_time = 0 (DISASM 0x264cb8-0x264cd4).
    void EndSkill();

    /// DeadChild (TYPED 943-956): in_skill = false; this_skill_time = 0.
    void DeadChild();

    /// One awake frame: SkillReload (cooldown count-up) + the Invoke countdown.
    void Tick(float dtMs);

    /// CreateTunder's nearest-target scan over @p hostiles (world units, scan order =
    /// collider order; strict `<` so the first of equal distances wins). @return the
    /// index into @p hostiles, or -1. @p blocked is the "Wall" Linecast.
    static int PickNearestTarget(const std::vector<SkillTarget> &hostiles, glm::vec2 origin,
                                 const SkillLineBlockedFn &blocked);

    bool InSkill() const { return m_InSkill; }
    /// FAITHFUL: RoleAttributePlayer.get_skill_ready -- skill_cd <= this_skill_time.
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }
    float CooldownRemaining() const;
    float SkillCd() const { return m_SkillCd; }
    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    float m_SkillCd = 0.0F;
    float m_ThisSkillTime = 0.0F;
    bool m_InSkill = false;
    bool m_TimerArmed = false;
    float m_InvokeLeftMs = 0.0F;
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C03_HPP */
