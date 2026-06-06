#ifndef GAME_BOSS_AI_NIAN_HPP
#define GAME_BOSS_AI_NIAN_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAINian
 * @brief Faithful phase + invisibility logic for the Nian (Niu Nian) Lunar New
 *        Year boss (RGEController subclass).
 *
 * Per-content port (report #5) of the Nian's signature damage-immunity tell.
 * Models the pure, unit-testable decision/cadence/phase brain over the
 * deterministic RGRandom stream; the actual SpriteRenderer alpha fade, the
 * Invoke("BackInvisible", 2f) scheduling, bullet/lantern spawns and Animator
 * triggers are scene/entity wiring left to the owning entity.
 *
 * SIGNATURE MECHANIC -- invisibility / damage immunity:
 *   - TurnInvisible(): only fires when currently visible; fades body alpha to
 *     0.5 and schedules a return after kInvisibleDuration (2s). We model the
 *     alpha as a flag + a countdown timer.
 *   - While invisible, OnHurt(...) is a NO-OP: the boss absorbs all damage (the
 *     teleport/phase-out tell). FAITHFUL: GetHurt returns when invisible.
 *   - BackInvisible(): after the window elapses, alpha restores to 1.0 and the
 *     boss is hurtable again. Modeled by the timer reaching 0 in Tick().
 *
 * ANGRY phase: at hp/max_hp < 0.5, BossAngry() fires once: Animator speed 1.2x,
 * shoot_cd halved (faster fans).
 *
 * @see recreation BossAINian.cs
 * @see FAITHFUL: BossAINian @ game_full.c:960290 (GetHurt), :960657
 *      (TurnInvisible), :960690 (BackInvisible), :960339 (BossAngry).
 */
class BossAINian {
public:
    /// Seconds the invisibility / immunity window lasts (Invoke delay 2f, 0x40000000).
    static constexpr float kInvisibleDuration = 2.0F;
    /// Body sprite alpha while phased out (0x3f000000).
    static constexpr float kInvisibleAlpha = 0.5F;
    /// Body sprite alpha while visible / hurtable (0x3f800000).
    static constexpr float kVisibleAlpha = 1.0F;

    /// hp/max_hp below this fraction triggers the angry phase (BossAngry).
    static constexpr float kAngryHpFraction = 0.5F;
    /// shoot_cd multiplier on entering angry (attacks ~2x faster).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// Animator speed on entering angry (0x3f99999a == 1.2f).
    static constexpr float kAngryAnimSpeed = 1.2F;

    explicit BossAINian(float baseShootCd);

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /**
     * @brief Enter the invisibility / damage-immunity window.
     *
     * FAITHFUL: BossAINian.TurnInvisible -- only fires when currently visible
     * (guarded by the @c invisible flag at 0xd4). Sets alpha to 0.5 and arms a
     * 2s countdown after which the boss phases back in (BackInvisible).
     * @return true if the window was newly entered, false if already invisible.
     */
    bool TurnInvisible();

    /**
     * @brief Advance the invisibility timer by @p dt seconds.
     *
     * When the countdown reaches 0 the boss phases back in (alpha 1.0, hurtable).
     * FAITHFUL: models the Invoke("BackInvisible", 2f) scheduled callback.
     * @return true on the frame the boss becomes visible again, else false.
     */
    bool Tick(float dt);

    /// True while phased out (damage-immune).
    bool IsInvisible() const { return m_Invisible; }
    /// Current body sprite alpha implied by the visibility state.
    float Alpha() const { return m_Invisible ? kInvisibleAlpha : kVisibleAlpha; }
    /// Seconds left in the current invisibility window (0 when visible).
    float InvisibleTimeLeft() const { return m_InvisibleTimer; }

    /**
     * @brief Apply a hit's resulting HP. Absorbed (ignored) while invisible.
     *
     * FAITHFUL: BossAINian.GetHurt -- returns early when invisible (0xd4); the
     * boss takes no damage and the angry check is skipped. When visible, applies
     * the hit and enters the angry phase once at hp/max_hp < 0.5.
     *
     * This logic class does not own the HP value (the entity does); the caller
     * passes the post-hit hp it WOULD apply. When this returns true (absorbed)
     * the caller must NOT apply the damage.
     *
     * @param hpAfter the hp the boss would have after this hit.
     * @param maxHp the boss's max hp.
     * @return true if the hit was absorbed (invisible, no-op); false if it landed.
     */
    bool OnHurt(int hpAfter, int maxHp);

    bool Angry() const { return m_Angry; }
    float ShootCd() const { return m_ShootCd; }
    float AnimSpeed() const { return m_AnimSpeed; }

    /**
     * @brief Deterministic roll deciding whether to phase out this cycle.
     *
     * The exact ShootReflection bucketing that schedules TurnInvisible is
     * inlined in the il2cpp build and not recoverable; we expose a single
     * Range(0,100) draw against a threshold so the decision is reproducible and
     * keeps the unit's stream in lockstep. See manual_flags.
     * @param chancePercent probability in [0,100] of entering invisibility.
     * @return true if the roll selects invisibility.
     */
    bool RollInvisibility(int chancePercent);

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};
    bool m_Angry = false;
    bool m_Invisible = false;
    float m_InvisibleTimer = 0.0F;
    float m_ShootCd;
    float m_AnimSpeed = 1.0F;
};

} // namespace Game

#endif /* GAME_BOSS_AI_NIAN_HPP */
