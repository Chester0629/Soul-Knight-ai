#ifndef GAME_CHAR_SKILL_C11_HPP
#define GAME_CHAR_SKILL_C11_HPP

#include "combat/SkillWorld.hpp"
#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class HolyLightZone
 * @brief The BulletGreenLight object C11's RoleSkill spawns at the player (the
 *        Holy_light_c11 prefab's script is BulletGreenLight, NOT BulletHolyLight --
 *        guid cabb7e00... in rgprefab/item/Holy_light_c11.prefab), engine-free.
 *
 * Evidence (DISASM 0xafa2e4 SetSourceObject / 0xafa3b8 TurnCanHit / 0xafa438
 * FixedUpdate / 0xafaacc GasEnd; T-39): SetSourceObject sets source_objcet, runs
 * TurnCanHit (can_hit = true; Invoke(TurnCanHit, 0.8f) -- movw 0xcccd/movt 0x3f4c
 * @0xafa3fc-0xafa404 -- so it re-arms every 0.8 s), arms Invoke(GasEnd, 1.8f) (movw
 * 0x6666/movt 0x3fe6 @0xafa334-0xafa338) and Destroy(gameObject, 4.5f) (movt 0x4090
 * @0xafa390). FixedUpdate: `if (can_hit && !is_end) { can_hit = false; pulse }`. A pulse
 * is CircleCastAll(position, radius 4.0 [movt 0x4080 @0xafa598], layer "Body_P") and,
 * per hit with a RoleAttribute, RestoreHealth(buff_digestive ? 2 : 1) [moveq/movne
 * @0xafa78c-0xafa794] (+ the BuffArmor child when `strengthen`, T-42). A hit that is an
 * RGPetController is healed 5 [mov r1,#5 @0xafa9e4] -- pets are NOT PORTED (T-47).
 * GasEnd sets is_end, so exactly three pulses land (t ~ 0, 0.8, 1.6 s).
 */
class HolyLightZone {
public:
    static constexpr float kPulseIntervalSec = 0.8F;
    static constexpr float kGasEndSec = 1.8F;
    static constexpr float kLifetimeSec = 4.5F;
    static constexpr float kRadiusUnits = 4.0F;
    static constexpr int kHealNormal = 1;
    static constexpr int kHealDigestive = 2;

    /// SetSourceObject: the light sits at @p centre (the player's position at cast).
    /// @p buffDigestive is RGGameProcess.buff_digestive (T-42: unwired -> false).
    void Start(glm::vec2 centre, bool buffDigestive);

    /// One fixed step (Update-phase Invokes first, then FixedUpdate). @param playerPos
    /// the player's position this step. @return the HP healed this step (0 or 1/2).
    int Step(float dtSec, glm::vec2 playerPos);

    bool Alive() const { return m_Alive; }
    int Pulses() const { return m_Pulses; }

private:
    bool m_Alive = false;
    bool m_CanHit = false;
    bool m_IsEnd = false;
    bool m_Digestive = false;
    float m_TurnTimer = 0.0F;
    float m_GasTimer = 0.0F;
    float m_Life = 0.0F;
    int m_Pulses = 0;
    glm::vec2 m_Centre{0.0F, 0.0F};
};

/**
 * @class CharSkillC11
 * @brief Skill brain for hero C11 (the Priest: C11Controller, an RGController player).
 *
 * AUDIT (source -> status). TYPED = game_typed.c lines visible in the cited range
 * (FAITHFUL). DISASM = libil2cpp.so ARM disassembly past the typed truncation (T-39).
 *
 *  - Update (TYPED 3521-3530): awake -> AttributeUpdate (SkillReload, even in_skill) +
 *    SeachUpdate. No in_skill_time countdown. FAITHFUL.
 *  - RoleSkill gate (TYPED 3552-3580): awake && skill_ready && !in_skill. FAITHFUL.
 *    Past the gate, DISASM 0x26ac10-0x26af94: BuffStatue.UseSkill hook (already the sim
 *    "skill_use" event); in_skill = 1 (@0x26ad10); Instantiate(skill_obj = the
 *    Holy_light_c11 prefab) at the player's position with identity rotation, parented
 *    to the scene container; OffensiveInterface.SetSourceObject(player) (starts the
 *    HolyLightZone); BulletGreenLight.strengthen (+0x12) = skill_strengthen (T-42);
 *    PlayEffect(clip_skill) (presentation, T-51); then vcall slot 25 = RoleSkillEnd
 *    (@0x26af90) in the SAME call -- the ultimate is instantaneous, the lingering light
 *    is its own object.
 *  - RoleSkillEnd (TYPED 3587-3601): in_skill = false ; ReSetSkillReload (this_skill_time
 *    = 0) ; UpdateShadowLock tint (T-51). FAITHFUL.
 *  - The light's behaviour: HolyLightZone (DISASM, see there). NOT PORTED (T-47): the
 *    pet heal (RGPetController -- the sim has no pets), the strengthened BuffArmor
 *    child (T-42), the heal-flash / GasEnd animation prefabs.
 *
 * NO RGRandom draw anywhere (Rng parity-only).
 */
class CharSkillC11 {
public:
    /// @param skillCd cooldown length, seconds (clamped >= 0). Starts READY.
    explicit CharSkillC11(float skillCd);

    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// RoleSkill (caller checks awake): gate, in_skill = 1, spawn the light, then
    /// RoleSkillEnd in the same call. @return true when the light was spawned.
    bool TryActivateSkill();

    /// One-shot: true once after a successful RoleSkill (the adapter starts the zone).
    bool ConsumeSpawn();

    /// RoleSkillEnd (TYPED 3587-3601): in_skill = false ; this_skill_time = 0.
    void EndSkill();

    /// One awake frame: SkillReload only (TYPED 3521-3530).
    void Tick(float dtMs);

    bool InSkill() const { return m_InSkill; }
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }
    float CooldownRemaining() const;
    float SkillCd() const { return m_SkillCd; }
    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    float m_SkillCd = 0.0F;
    float m_ThisSkillTime = 0.0F;
    bool m_InSkill = false;
    bool m_SpawnPending = false;
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C11_HPP */
