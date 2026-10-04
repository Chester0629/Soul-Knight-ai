#ifndef GAME_CHAR_SKILL_C12_HPP
#define GAME_CHAR_SKILL_C12_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC12
 * @brief Skill brain for hero C12 (the Druid: C12Controller, an RGController player).
 *
 * AUDIT (source -> status). TYPED = game_typed.c lines visible in the cited range
 * (FAITHFUL). DISASM = libil2cpp.so ARM disassembly past the typed truncation (T-39).
 *
 *  - Update (TYPED 3674-3683): awake -> AttributeUpdate (SkillReload, unconditional) +
 *    SeachUpdate. No in_skill_time countdown. FAITHFUL.
 *  - RoleSkill gate (TYPED 3706-3729): awake && skill_ready ONLY -- unlike C03/C05/C07/
 *    C11 there is NO `!in_skill` test (the typed body goes straight from
 *    get_skill_ready to the get_transform tail). FAITHFUL. The body past the gate is
 *    DISASM 0x26b174-0x26b7d4 (the orphan fragment FUN_0026b1dc, game_full.c:158476-
 *    158512, is the same code):
 *      * BuffStatue.UseSkill hook (every press that passes the gate).
 *      * if in_skill != 0 [@0x26b26c-0x26b274]: PHASE CUT -- RGHand.AtkCut(hand,
 *        get_facing() [vcall slot 4 @0x26b28c], role_attribute.atk (+0x40),
 *        role_attribute.critical (+0x24)) @0x26b2e0, then vcall slot 25 = RoleSkillEnd
 *        @0x26b2fc (tail call). A bare-handed melee swing of damage = atk.
 *      * else: PHASE SUMMON -- in_skill = 1 [@0x26b308]; Instantiate(skill_obj1 = wolf1)
 *        at the player's position + (0.5,0,0) [pool 0x3f000000 @0x26b340] and
 *        Instantiate(skill_obj2 = wolf2) at (-0.5,0,0) [0xbf000000 @0x26b548], each
 *        RGPetController.SetMaster(player) and parented to the scene container; when
 *        skill_strengthen both WolfController.strengthen (+0x80) = true; PlayEffect
 *        (skill_clips[GetSkinIndex()] when that index exists, else clip_skill);
 *        then RoleSkillEnd @0x26b7d4.
 *  - RoleSkillEnd (TYPED 3736-3749): ReSetSkillReload (this_skill_time = 0) ;
 *    UpdateShadowLock tint (T-51). FAITHFUL fact: C12 RoleSkillEnd (game_typed.c
 *    3736-3749) does not store in_skill (unlike C01/C02/C04/C05/C06/C07/C11, whose
 *    RoleSkillEnd each contain an in_skill = false store).
 *    INFERRED (T-49): no other clear site was found (searched game_typed.c / game_full.c
 *    for stores to RGController.in_skill, offset 0x55: only the C01-C07/C11 clears and the
 *    C02/C06/C07/C11/C12 sets exist), so in_skill is TREATED as staying set after the first
 *    summon and every later cast is the CUT phase. This is an inference from the absence
 *    of a store in the recovered code, NOT proof the original never clears it.
 *  - NOT PORTED: the two wolf pets (RGPetController/WolfController -- the sim has no
 *    ally entities, T-48): the summon phase only emits its cue (count 2, strengthened
 *    flag); the AtkCut RGSword prefab's own hitbox is the sim's T-36 melee reach.
 *
 * NO RGRandom draw anywhere (Rng parity-only).
 */
class CharSkillC12 {
public:
    /// DISASM pool offsets of the two wolf spawn points, world units (+x / -x of the
    /// player): 0x3f000000 = 0.5f @0x26b340, 0xbf000000 = -0.5f @0x26b548.
    static constexpr float kWolf1OffsetX = 0.5F;
    static constexpr float kWolf2OffsetX = -0.5F;
    static constexpr int kWolfCount = 2;

    enum class Phase { None, Summon, Cut };

    struct RoleSkillResult {
        Phase phase = Phase::None;
        int cutDamage = 0;       ///< Phase::Cut: AtkCut atk argument.
        int cutCritical = 0;     ///< Phase::Cut: AtkCut critical argument (T-48: unused by the sim cut).
        int wolves = 0;          ///< Phase::Summon: pets summoned (2).
        bool strengthened = false; ///< Phase::Summon: WolfController.strengthen.
    };

    /// @param skillCd cooldown length, seconds (clamped >= 0). Starts READY, not in_skill.
    explicit CharSkillC12(float skillCd);

    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// RoleSkill (caller checks awake): gate = skill_ready only; then the phase by
    /// in_skill (see the audit) and RoleSkillEnd (cooldown restart) in the same call.
    /// @param atk role_attribute.atk ; @param critical role_attribute.critical ;
    /// @param skillStrengthen T-42 (false).
    RoleSkillResult RoleSkill(int atk, int critical, bool skillStrengthen);

    /// RoleSkillEnd (TYPED 3736-3749): ReSetSkillReload ONLY (this_skill_time = 0).
    void EndSkill();

    /// One awake frame: SkillReload only (TYPED 3674-3683).
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
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C12_HPP */
