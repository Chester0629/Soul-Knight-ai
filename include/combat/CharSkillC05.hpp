#ifndef GAME_CHAR_SKILL_C05_HPP
#define GAME_CHAR_SKILL_C05_HPP

#include <vector>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC05
 * @brief Skill brain for hero C05 (the Alchemist: C05Controller, an RGController player).
 *
 * AUDIT (source -> status). TYPED = game_typed.c lines visible in the cited range
 * (FAITHFUL). DISASM = libil2cpp.so ARM disassembly past the typed truncation (T-39;
 * file offset = Ghidra address - 0x10000, reproducible with tools/arm_disasm.py).
 *
 *  - Update (TYPED 1517-1526): awake -> AttributeUpdate (SkillReload cooldown count-up,
 *    every awake frame, even in_skill) + SeachUpdate. No in_skill_time countdown. FAITHFUL.
 *  - RoleSkill gate (TYPED 1549-1577): awake && skill_ready && !in_skill. FAITHFUL.
 *    Past the gate the typed body ends in the get_transform tail; DISASM 0x265ccc-0x2661ec:
 *      * BuffStatue.UseSkill hook (Find "buff_statue") -- already the sim "skill_use"
 *        event; presentation otherwise (T-51).
 *      * in_skill = 1 (strb [this,#0x55] @0x265dc8).
 *      * count = skill_strengthen ? 3 : 1 (`mov r1,#1; movwne r1,#2; add r8,r1,r0`
 *        @0x265df4-0x265e00) bottles; iteration i throws bottle_objs[GetCurrentSkin(4)]
 *        (the hero's CURRENT SKIN picks the variant, falling back to element 0 when the
 *        array is shorter, @0x265ef4-0x265f88) at a fan offset -20*strengthen + 20*i
 *        degrees (sb = -20 or 0, += 0x14 per bottle @0x265e0c/0x2661a4).
 *      * PlayEffect(clip_skill) @0x2661d8, then vcall slot 25 = RoleSkillEnd
 *        @0x2661e8 -- IN THE SAME CALL, so the ultimate is instantaneous: it is
 *        "in skill" only inside RoleSkill itself and the cooldown starts at once.
 *  - RoleSkillEnd (TYPED 1584-1638): in_skill = false ; ReSetSkillReload (this_skill_time
 *    = 0) ; shadow_lock tint by get_skill_ready (presentation, T-51). FAITHFUL; the
 *    two tint colours are code immediates (0x3e70f0f1/0x3f69e9ea/0x3e969697 ready,
 *    0x3dd8d8d9/0x3ec8c8c9/0x3ca0a0a1 not) and are NOT PORTED.
 *  - The thrown BulletBottle flight, the explosion and the Gas zone (rgprefab Gas:
 *    damage 1, hit_player/hit_enemy 1) are NOT PORTED (T-44): the sim has neither the
 *    bouncing bottle projectile nor lingering damage zones; the port emits the
 *    "bottle_throw" cue (count / variant / fan) so the effect is visible in the log.
 *
 * NO RGRandom draw anywhere in the ported body (the bottle RNG, if any, is inside the
 * NOT PORTED bullet), so the Rng member is parity-only.
 */
class CharSkillC05 {
public:
    /// DISASM 0x265df4-0x265e00 (see audit).
    static constexpr int kBottlesNormal = 1;
    static constexpr int kBottlesStrengthened = 3;
    /// Per-bottle fan step in degrees (add sb,sb,#0x14 @0x2661a4); the first offset is
    /// -20 when strengthened, else 0 (sbfx/lsl @0x265e04-0x265e10).
    static constexpr float kFanStepDeg = 20.0F;

    /// The outcome of one RoleSkill press.
    struct RoleSkillResult {
        bool activated = false;       ///< the gate passed (a bottle volley was thrown).
        int bottleCount = 0;          ///< 1, or 3 when skill_strengthen.
        std::vector<float> fanOffsetsDeg; ///< per-bottle angle offset, degrees.
        int variantIndex = 0;         ///< bottle_objs index (the hero's current skin).
        int variantCount = 0;         ///< bottle_objs.Length from the prefab/data.
    };

    /// @param skillCd cooldown length, seconds (clamped >= 0). Starts READY.
    explicit CharSkillC05(float skillCd);

    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// RoleSkill (caller checks awake): gate, in_skill = 1, volley, then RoleSkillEnd in
    /// the same call. @param skinIndex RGSaveManager.GetCurrentSkin(4) (0 in the sim:
    /// no skin system). @param variantCount bottle_objs.Length (CharacterDef::
    /// skillRefs["bottle_objs"].size(), prefab data). @param skillStrengthen T-42 (false).
    RoleSkillResult RoleSkill(int skinIndex, int variantCount, bool skillStrengthen);

    /// Thin wrapper for the brain-level tests / uniform callers: RoleSkill(0, 4, false).
    bool TryActivateSkill();

    /// RoleSkillEnd (TYPED 1584-1638): in_skill = false ; this_skill_time = 0.
    void EndSkill();

    /// One awake frame: SkillReload only (TYPED 1517-1526).
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

#endif /* GAME_CHAR_SKILL_C05_HPP */
