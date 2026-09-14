#ifndef GAME_CHAR_SKILL_C04_HPP
#define GAME_CHAR_SKILL_C04_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class CharSkillC04
 * @brief Faithful, engine-free skill/attack brain for hero C04 (a melee sword
 *        hero: C04Controller, an RGController player subclass).
 *
 * Per-content port. Models ONLY the pure, unit-testable decisions recovered in
 * the C04Controller bodies; the actual animator/SFX/transform/Instantiate/
 * renderer wiring is left to the owning entity (referenced in comments only).
 *
 * C04 is a charge-and-cut melee hero. Its gimmick (decoded from the decomp's
 * own self-consistent field accesses, cross-checked vs CharSkillC01 + the IL2CPP
 * field names C04Controller.{in_skill_effect, has_cut, combo}):
 *   - The ultimate (RoleSkill) puts the hero into a sword-cut "effect" window
 *     (in_skill_effect, this+0x94). While that window is up, the NEXT press does
 *     one big sword cut (AtkCut) instead of normal hand fire, and latches has_cut
 *     (this+0x95) so a held button does not re-cut.
 *   - Killing enemies WHILE in that effect drives a combo counter (combo,
 *     this+0x98). Every kill that is NOT the 5th instantly refreshes the skill
 *     cooldown (RoleAttributePlayer.ReflashSkillCd -> this_skill_time = skill_cd,
 *     i.e. immediately ready again); the 5th kill wraps the combo back to 0 and
 *     does NOT refresh. This "chain your cuts to keep the skill up" loop is the
 *     real modeled brain here.
 *
 * WHAT THE DECOMP SHOWS (grounded; no reconstruction of unseen ability effects):
 *
 *  - RoleSkill (C04Controller__RoleSkill @ game_full.c:156371):
 *      The hero ultimate gate, same shape as the base RGController one:
 *        if (awake)                                 // this+0xC  == param_1[3]
 *          if (role_attribute != null)              // this+0x44 == param_1[0x11]
 *            if (role_attribute.skill_ready)        //   get_skill_ready
 *              if (!in_skill)                        // this+0x55
 *                <enter skill>                       // get_transform tail truncated
 *      The post-gate effect is a get_transform tail-call (the hero-specific sword
 *      spawn), so only the GATE is recoverable. NO RGRandom draw on this path.
 *      The cooldown engine (this_skill_time count-up to skill_cd) is the
 *      established PlayerDash / CharSkillC01 pattern; reused so TryActivateSkill
 *      can be unit-tested end to end (see fabrication_flags).
 *
 *      AUDIT FIX (game_typed.c:1214-1263, fully recovered -- NOT a truncated
 *      tail-call as the prior game_full.c-line-number comment here claimed):
 *      C04Controller__RoleSkillEnd is fully visible and writes, in order,
 *      in_skill = false; has_cut = false; role_attribute.ReSetSkillReload();
 *      (then a pure owner/visual tail: get_skill_ready + shadow_lock sprite
 *      recolor). EndSkill() now clears has_cut too, matching the decomp.
 *
 *  - RoleAtk (C04Controller__RoleAtk @ game_full.c:156423, value = press down):
 *      if (awake) {                                  // this+0xC
 *        if (in_item && press && !in_skill)          // this+0x55
 *          { TriggerItem(); return; }                // use the pickup
 *        if (in_skill_effect == 0) {                 // this+0x94 low byte
 *          if (press && <aim-virtual>)               // *(this->vt+0xe4) predicate
 *            if (!hand.IsMelee()) <aim nudge>;        // get_position tail (owner)
 *          hand.SetAttack(value);                     // normal fire/stop
 *        } else {                                     // sword-effect window is up
 *          if (has_cut != 0)                          // this+0x95: already cut
 *            { hand.SetAttack(value); return; }
 *          if (press) {                               // do the one big cut
 *            int dmg = atk + (skill_strengthen ? 7 : 3);
 *            AtkCut(dmg);                              // owner: SFX + spawn RGSword
 *            has_cut = 1;                              // latch: one cut per window
 *          }
 *        }
 *      }
 *      NO RGRandom draw. The damage literals 3 and 7 are immediate operands; the
 *      atk base is role_attribute+0x40. The branch order is preserved exactly.
 *
 *  - KillSomeOne (C04Controller__KillSomeOne @ game_full.c:156499):
 *      if (in_skill_effect) {                         // this+0x94
 *        combo += 1;                                  // this+0x98
 *        if (combo == 5) combo = 0;                   // wrap; no refresh
 *        else { ReflashSkillCd(); UpdateShadowLock(); } // instant skill-ready
 *        CancelInvoke("..."); EndSkillEffect();        // owner: end the window
 *      }
 *      base RGController.KillSomeOne();
 *      NO RGRandom draw. ReflashSkillCd sets this_skill_time = skill_cd (ready
 *      now); the EndSkillEffect (speed-rate bonus removed + trail renderer
 *      disable + clear in_skill_effect) is owner-side except for clearing the
 *      ONE flag it writes (in_skill_effect, this+0x94), which the brain models
 *      so the next window's gate starts clean. has_cut (this+0x95) is NOT
 *      cleared by KillSomeOne / EndSkillEffect -- it is cleared by
 *      RoleSkillEnd instead (see EndSkill() / AUDIT FIX above).
 *
 *  - EndSkillEffect (C04Controller__EndSkillEffect @ game_typed.c:1276-1303,
 *      fully recovered): OWNER body -- role_attribute.speed_rate +=
 *      DAT_0026577c (removes a speed-rate bonus applied elsewhere; AUDIT FIX:
 *      the prior game_full.c-based comment here wrongly claimed "energy
 *      attr+0x14 -= 1"; no such write is present), trail.set_enabled(false)
 *      at this+0x90. Its SOLE brain write is the single byte clear
 *      *(undefined1 *)(this + 0x94) = 0;  // in_skill_effect = 0 (ONLY 0x94)
 *      It does NOT write has_cut (0x95); has_cut is cleared by RoleSkillEnd,
 *      not by EndSkillEffect / KillSomeOne (see fabrication_flags item (c)).
 *
 *  - AutoLock / AtkCut / Awake / _ctor: OWNER bodies (transform/get_position spawn
 *      tails, RGMusicManager + Instantiate<RGWeapon>/RGSword, AwakeController, base
 *      ctor). No pure brain state; everything is delegated to the owner.
 *
 * Determinism: NO recovered body draws from rg_random, so this unit makes ZERO
 * RNG draws (the RGRandom member is carried for interface parity and to keep the
 * deterministic stream untouched; Seeded() confirms the seed without advancing).
 *
 * fabrication_flags (the ONLY reconstructions in this unit; everything else is the
 * grounded decomp above -- each is flagged inline at its call site too):
 *   (a) Cooldown engine reuse. C04Controller has NO recovered Update body (IL2CPP
 *       Update is empty), so Tick() / SkillReady() / CooldownRemaining() reuse the
 *       established PlayerDash / CharSkillC01 / RoleAttributePlayer.SkillReload
 *       model: this_skill_time counts UP by dt, clamped to skill_cd; ready when
 *       skill_cd <= this_skill_time. Unlike C01 there is NO in_skill_time countdown,
 *       so Tick() does NOT auto-end the skill (no Update/auto-end exists in C04).
 *   (b) EndSkill() = C04Controller__RoleSkillEnd (game_typed.c:1214-1263,
 *       FAITHFUL, fully recovered -- see AUDIT FIX above): in_skill = false;
 *       has_cut = false; role_attribute.ReSetSkillReload() (this_skill_time =
 *       0). Modeled directly; not a reconstruction (kept in fabrication_flags
 *       only because the owner-side shadow_lock recolor tail is not modeled).
 *   (c) Activation window-open. The recovered RoleSkill head (156371) writes NO
 *       flags -- it is a get_transform tail-call past the gate. TryActivateSkill's
 *       in_skill = true and the window-open (in_skill_effect = true, has_cut =
 *       false) live in the owner-side transform tail / Invoke, modeled here so the
 *       activate->cut->kill loop is one testable decision. RELATED: EndSkillEffect
 *       (game_typed.c:1276-1303) clears ONLY in_skill_effect (0x94); has_cut
 *       (0x95) is cleared by RoleSkillEnd (see (b)), not by EndSkillEffect /
 *       KillSomeOne, so a window that ends via a kill leaves has_cut set until
 *       the hero's NEXT RoleSkillEnd or TryActivateSkill re-clears it.
 *
 * @see CharSkillC01.hpp (the canonical hero brain shape + cooldown-reuse +
 *      fabrication-flag conventions), recreation Player/RGController.cs,
 *      PlayerDash.hpp (the cooldown pattern), skeleton C04Controller.cs.
 */
class CharSkillC04 {
public:
    /// Bonus sword-cut damage added to base atk when skill_strengthen is OFF.
    /// FAITHFUL: immediate operand 3 in RoleAtk (iVar2 = 3 default).
    static constexpr int kCutBonusNormal = 3;
    /// Bonus sword-cut damage when skill_strengthen is ON.
    /// FAITHFUL: immediate operand 7 in RoleAtk (iVar2 = 7 if strengthen).
    static constexpr int kCutBonusStrengthened = 7;
    /// The combo length at which the chain wraps and stops refreshing the cd.
    /// FAITHFUL: KillSomeOne compares combo == 5.
    static constexpr int kComboWrap = 5;

    /**
     * @brief Construct the skill brain from the hero's skill timing stats.
     * @param skillCd cooldown length in seconds (RoleAttributePlayer.skill_cd,
     *                attr+0x44). Clamped to >= 0. From the CharacterDef stat sheet.
     *
     * Starts READY (this_skill_time == skill_cd) and NOT in skill / NOT in the
     * sword-effect window -- a freshly set-up hero whose first ultimate is
     * available (matches PlayerDash / CharSkillC01).
     */
    explicit CharSkillC04(float skillCd);

    /// Seed the deterministic stream (kept untouched; no draw is ever made).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Attempt to activate the hero ultimate this frame.
     *
     * FAITHFUL (gate): C04Controller__RoleSkill @ 156371 -- requires
     * awake && skill_ready && !in_skill. (The awake gate is the caller's
     * responsibility.) Past the gate the recovered RoleSkill head is purely a
     * get_transform tail-call (owner: hero-specific sword spawn) -- it writes NO
     * flags.
     *
     * RECONSTRUCTED (flagged; see fabrication_flags item (c)): on success this also
     * sets in_skill = true and OPENS the sword-cut effect window
     * (in_skill_effect = true, has_cut = false) so the next press performs a cut.
     * Those three writes are NOT in the recovered RoleSkill body -- they live in the
     * owner-side transform tail / Invoke -- but are modeled here so TryActivateSkill
     * / RoleAtk form one testable decision, in the same flagged style as the reused
     * cooldown engine. The cooldown spend is an EndSkill concern.
     *
     * @return true if the skill activated this call (state entered), else false.
     */
    bool TryActivateSkill();

    /**
     * @brief End the active skill and spend the charge.
     *
     * FAITHFUL (base): C04Controller__RoleSkillEnd @ game_typed.c:1214-1263 (fully
     * recovered). Clears in_skill AND has_cut, then spends the cooldown via
     * RoleAttributePlayer.ReSetSkillReload (this_skill_time = 0).
     *
     * PORT CHOICE (T-37): the decomp never shows WHAT calls RoleSkillEnd for C04 --
     * no Invoke/CancelInvoke site or auto-end timer references it anywhere in the
     * recovered bodies (unlike C01/C10/C13's in_skill_time countdown), so the real
     * end trigger is genuinely not visible. The port's adapter (CharSkillC04Adapter)
     * uses a second skill-button press as the end trigger (the toggle shape shared
     * with C08). That collides with KillSomeOne's ReflashSkillCd refresh: if this
     * body did an unconditional this_skill_time=0 here, a toggle-close pressed
     * shortly after an in-window kill would silently ERASE the refresh the kill just
     * granted (the player would see the skill go on cooldown despite the game
     * telling them, via the kill, that it was instantly ready again). So EndSkill()
     * skips the this_skill_time reset when RefreshedThisWindow() is true (a kill
     * refreshed the cooldown since the window opened) -- the cooldown stays exactly
     * where the refresh left it (ready, or ready-then-untouched-by-the-5th-kill's
     * wrap). When no kill has refreshed this window, EndSkill spends the cooldown
     * normally (this_skill_time=0), matching ReSetSkillReload exactly.
     *
     * Also clears the sword-effect window (in_skill_effect) here, NOT just has_cut:
     * the decomp's RoleSkillEnd does not touch in_skill_effect (it is normally
     * closed only by KillSomeOne/EndSkillEffect) -- but since this port's chosen end
     * trigger can fire BEFORE any kill closes the window (the player cancels the
     * ultimate with no kill), leaving in_skill_effect set would let a 3rd press
     * (after a fresh TryActivateSkill gate re-opens... no, TryActivateSkill always
     * re-opens it explicitly) or, more importantly, any RoleAtk call between this
     * EndSkill and the next TryActivateSkill still see in_skill_effect==true and
     * throw a cut with no active ultimate. Clearing it here is the one extra write
     * needed so "no cut happens after the end" holds unconditionally, exactly as
     * requested; it is a superset of the decomp's own EndSkillEffect clear, not a
     * contradiction of it (EndSkillEffect still separately clears it on the kill
     * path, so both routes agree on the end state).
     *
     * No-op if not currently in skill.
     */
    void EndSkill();

    /**
     * @brief Decision returned by RoleAtk -- what the owning entity must do.
     *
     * FAITHFUL: C04Controller__RoleAtk @ 156423. All-false when !awake or when a
     * branch issues no hand/cut action.
     */
    struct AtkDecision {
        /// in_item && press && !in_skill -> the press triggers the pickup
        /// (RGController.TriggerItem) and returns; no hand/cut action.
        bool triggerItem = false;
        /// Should hand.SetAttack(value) be issued? (normal-fire path, or the
        /// already-cut path while the effect window is up.)
        bool setHandAttack = false;
        /// The value passed to hand.SetAttack (== pressDown when setHandAttack).
        bool handAttackValue = false;
        /// During the sword-effect window, a fresh press performs ONE big cut
        /// (owner: C04Controller.AtkCut). True only on that path.
        bool doCut = false;
        /// The damage handed to AtkCut: atk + (skill_strengthen ? 7 : 3).
        /// Valid only when doCut.
        int cutDamage = 0;
    };

    /**
     * @brief Process an attack press/release (C04Controller__RoleAtk @ 156423).
     *
     * Branch order preserved exactly:
     *   awake gate -> (in_item && press && !in_skill -> TriggerItem)
     *   -> if !in_skill_effect: normal hand.SetAttack(value)
     *   -> else if has_cut: hand.SetAttack(value)
     *   -> else if press: AtkCut(atk + bonus) and latch has_cut.
     * Performing a cut latches has_cut so a held button cuts only once per window.
     *
     * @param pressDown        true = attack button down (value==1), false = release.
     * @param standingOnItem   true if standing on a pickup (RGController.in_item).
     * @param atk              role_attribute.atk base (attr+0x40) for cut damage.
     * @param skillStrengthen  RoleAttributePlayer.skill_strengthen (bonus 7 vs 3).
     * @return the decision the owning entity should act on (all-false if !awake is
     *         the caller's concern -- this models the awake-true body).
     */
    AtkDecision RoleAtk(bool pressDown, bool standingOnItem, int atk,
                        bool skillStrengthen);

    /**
     * @brief Register a kill (C04Controller__KillSomeOne @ 156499).
     *
     * FAITHFUL: only fires combo/cooldown logic while in the sword-effect window
     * (in_skill_effect, this+0x94). Each such kill advances the combo (this+0x98);
     * the 5th wraps it to 0 with NO cooldown refresh, every other kill refreshes
     * the cooldown to ready (RoleAttributePlayer.ReflashSkillCd -> this_skill_time
     * = skill_cd). Either way the effect window then ends (CancelInvoke +
     * EndSkillEffect: clears ONLY in_skill_effect, this+0x94). has_cut (this+0x95)
     * is NOT touched here -- no recovered C04 body clears it on a kill (it is
     * re-cleared on the next activation; see fabrication_flags). Outside the window
     * this is a no-op (the base RGController.KillSomeOne is the owner's concern).
     *
     * @return true if this kill counted toward the chain (was in the effect window).
     */
    bool KillSomeOne();

    /// @return true while the ultimate is active (in_skill, this+0x55).
    bool InSkill() const { return m_InSkill; }

    /// @return true while the sword-cut effect window is up (in_skill_effect,
    /// this+0x94) -- the window during which a press cuts and kills chain.
    bool InSkillEffect() const { return m_InSkillEffect; }

    /// @return true once a cut has fired in the current window (has_cut, this+0x95).
    bool HasCut() const { return m_HasCut; }

    /// @return the current combo count (combo, this+0x98).
    int Combo() const { return m_Combo; }

    /// PORT CHOICE (T-37, see EndSkill() doc): true once a KillSomeOne() in the
    /// CURRENT in_skill window has already refreshed the cooldown (ReflashSkillCd).
    /// Not part of the recovered C04 state (no this+offset backs it) -- it is
    /// sim-side bookkeeping the port's toggle-close end trigger needs so it does not
    /// clobber a refresh a kill just granted. Reset on TryActivateSkill (fresh
    /// window) and consumed/reset by EndSkill().
    bool RefreshedThisWindow() const { return m_RefreshedThisWindow; }

    /**
     * @return true when off cooldown and ready to fire.
     * FAITHFUL: RoleAttributePlayer.get_skill_ready -- skill_cd <= this_skill_time.
     */
    bool SkillReady() const { return m_ThisSkillTime >= m_SkillCd; }

    /// @return seconds until the cooldown finishes recharging (0 when ready).
    float CooldownRemaining() const;

    /// Configured cooldown length (skill_cd, attr+0x44), seconds.
    float SkillCd() const { return m_SkillCd; }

    /**
     * @brief One awake-frame tick: advance the cooldown.
     *
     * RECONSTRUCTED (flagged): C04Controller has NO recovered Update body (the
     * IL2CPP Update is empty), so this is the established CharSkillC01 /
     * RoleAttributePlayer.SkillReload model reused so the cooldown is testable:
     * this_skill_time counts UP by dt, clamped to skill_cd. C04 has no recovered
     * in_skill_time countdown / auto-end (unlike C01), so Tick does NOT auto-end
     * the skill -- the window ends via KillSomeOne / EndSkill only.
     *
     * @param dtMs frame delta in milliseconds (<= 0 is ignored).
     */
    void Tick(float dtMs);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};             ///< deterministic stream; never advanced here.
    float m_SkillCd = 0.0F;       ///< skill_cd (attr+0x44), cooldown length.
    float m_ThisSkillTime = 0.0F; ///< this_skill_time (attr+0x5c); counts up.
    bool m_InSkill = false;       ///< in_skill (this+0x55); ultimate active flag.
    bool m_InSkillEffect = false; ///< in_skill_effect (this+0x94); cut window up.
    bool m_HasCut = false;        ///< has_cut (this+0x95); cut latched this window.
    int m_Combo = 0;              ///< combo (this+0x98); kill-chain counter.
    bool m_RefreshedThisWindow = false; ///< T-37 sim-side bookkeeping, see EndSkill().
};

} // namespace Game

#endif /* GAME_CHAR_SKILL_C04_HPP */
