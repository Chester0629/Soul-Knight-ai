#ifndef GAME_BOSS_AI13_HPP
#define GAME_BOSS_AI13_HPP

#include <glm/glm.hpp>

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAI13
 * @brief Faithful state-machine / cadence brain for Soul Knight 1.7.10's
 *        BossAI13 (an RGEController subclass: the snowman-armed dual-hand boss).
 *
 * Per-content port. Models ONLY the pure, deterministic brain that an
 * RNG-replay must reproduce frame-for-frame; the owning entity still drives
 * animator triggers (StartAtk01..03 SetTrigger), bullet/RGWeapon Instantiate,
 * coroutines (Attacking5), Rigidbody2D velocity, audio (RGMusicManager) and the
 * BossInfo hp-bar. Those owner calls are named in one-line comments in the .cpp
 * and carry no brain logic here.
 *
 * The class draws from its private RGRandom stream in EXACTLY two places:
 *   - RunReflection(): two float draws Range(-1f, 1f) -> a random unit move
 *     direction (BossAI13__RunReflection @ game_full.c:957998). Ungated: the
 *     method is itself the Invoke target.
 *   - ShootReflection(): one int draw Range(0, 100) gated behind
 *     can_shoot && !dead && !dizzy && rg_random != null
 *     (BossAI13__ShootReflection @ game_full.c:958056). The roll->StartAtkNN
 *     jumptable could not be recovered from the decomp (indirect jump at
 *     0x00adb738); we keep the single draw so the stream stays in lockstep and
 *     leave attack dispatch to the owner (see ShootReflection()).
 *
 * State the decomp actually WRITES (modelled here; nothing invented):
 *   - angry(0xec) = 1 and shoot_cd(0x3c) *= 0.5 in BossAngry (once, hp<50%);
 *   - dizzy(0xa1) = 1 in Dizzy (gated on !dead);
 *   - target_obj(0x7c) = null in Scout (gated on !dead && !dizzy);
 *   - awake(0x18) = 1 in OnGameStateChange (room-ready), and awake = 0 in
 *     FixedUpdate's shooting-brake branch.
 *
 * @see recreation BossAI01.cs (sibling boss, identical GetHurt/BossAngry shape);
 *      FAITHFUL: BossAI13 @ game_full.c:957889-958539.
 */
class BossAI13 {
public:
    /// hp/max_hp strictly below this fraction triggers the angry phase (once).
    static constexpr float kAngryHpFraction = 0.5F;
    /// BossAngry multiplies shoot_cd(0x3c) by this (faster firing in phase 2).
    static constexpr float kAngryShootCdScale = 0.5F;
    /// BossAngry sets animator speed to 1.2 (0x3f99999a); owner-side, kept as a
    /// documented golden scalar for the test.
    static constexpr float kAngryAnimSpeed = 1.2F;

    /// RunReflection's random-direction component bounds (Range(-1f, 1f), max
    /// INCLUSIVE per Unity float semantics).
    static constexpr float kReflectComponentMin = -1.0F;
    static constexpr float kReflectComponentMax = 1.0F;

    /// ShootReflection's attack-selection roll ceiling (Range(0, 100), max
    /// EXCLUSIVE per Unity int semantics).
    static constexpr int kRollCeiling = 100;

    /// InAtk01 bullet count: 5 normally, 7 while angry (no RNG; angry-gated).
    static constexpr int kAtk01BulletsCalm = 5;
    static constexpr int kAtk01BulletsAngry = 7;

    BossAI13() = default;

    /// Seed the boss's deterministic stream (call once at spawn).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    // ---- gates / state (decoded against RGEController offset table) ----------

    bool Awake() const { return m_Awake; }   ///< awake @0x18
    bool Dead() const { return m_Dead; }     ///< dead  @0x38
    bool Dizzy() const { return m_Dizzy; }   ///< dizzy @0xa1
    bool Shooting() const { return m_Shooting; }     ///< shooting @0x80
    bool CanShoot() const { return m_CanShoot; }     ///< can_shoot @0x40
    bool Angry() const { return m_Angry; }   ///< angry @0xec
    bool HasTarget() const { return m_HasTarget; }   ///< target_obj @0x7c != null
    float ShootCd() const { return m_ShootCd; }      ///< shoot_cd @0x3c

    /// Test/setup helpers for the gates the decomp reads (these mirror fields the
    /// owning entity owns; they take NO RNG draw).
    void SetDead(bool v) { m_Dead = v; }
    void SetDizzy(bool v) { m_Dizzy = v; }
    void SetShooting(bool v) { m_Shooting = v; }
    void SetCanShoot(bool v) { m_CanShoot = v; }
    void SetHasTarget(bool v) { m_HasTarget = v; }
    void SetShootCd(float v) { m_ShootCd = v; }

    // ---- pure-logic bodies ---------------------------------------------------

    /**
     * @brief FixedUpdate's shooting-brake branch (the only field write it owns).
     *
     * FAITHFUL: BossAI13__FixedUpdate @ game_full.c:957902. The method is mostly
     * owner-side (FixedUpdateSeed + Rigidbody2D velocity from move_direction *
     * speed; the EnemyUpdate vtable tail). The one brain-visible write: while
     * awake && !shooting && dead, it zeroes velocity and clears awake(0x18)=0.
     * We model that single transition; velocity/transform stay with the owner.
     * @return true if awake was cleared this tick.
     */
    bool FixedUpdateBrake();

    /**
     * @brief Scout: target re-acquisition tick. Gated on !dead && !dizzy.
     *
     * FAITHFUL: BossAI13__Scout @ game_full.c:957973. When not dead/dizzy it
     * writes target_obj(0x7c) = null (then get_transform, owner-side). No RNG.
     * @return true if the gate passed and target_obj was cleared.
     */
    bool Scout();

    /**
     * @brief RunReflection: pick a random unit move direction (2 float draws).
     *
     * FAITHFUL: BossAI13__RunReflection @ game_full.c:957998. Draws
     * x = Range(-1f, 1f) then y = Range(-1f, 1f) (IN THAT ORDER), builds the
     * Vector2(x, y), normalizes it, and sets it as move_direction (animator
     * "walk"=true is owner-side). Ungated: this is the Invoke target itself.
     * @return the normalized direction (zero vector if both draws were 0).
     */
    glm::vec2 RunReflection();

    /**
     * @brief ShootReflection: gated attack-selection roll.
     *
     * FAITHFUL: BossAI13__ShootReflection @ game_full.c:958056. Gate order:
     * can_shoot(0x40) && !dead(0x38) && !dizzy(0xa1); only then (and only if
     * rg_random != null, always true here once seeded) does it draw
     * Range(0, 100). The roll->StartAtkNN jumptable was NOT recovered (indirect
     * jump at 0x00adb738), so dispatch is the owner's; the single draw keeps the
     * stream in lockstep regardless.
     * @param outRoll receives the roll in [0,100) when the gate passes.
     * @return true if the gate passed and a draw was taken (stream advanced).
     */
    bool ShootReflection(int &outRoll);

    /**
     * @brief GetHurt -> BossAngry: enter the angry phase once at hp/max_hp < 0.5.
     *
     * FAITHFUL: BossAI13__GetHurt @ game_full.c:958176 -> BossAI13__BossAngry
     * @ game_full.c:958218. GetHurt is gated on awake(0x18) && !dead(0x38); the
     * angry check is (hp / max_hp < 0.5) && !angry(0xec). BossAngry writes
     * angry(0xec)=1 and shoot_cd(0x3c) *= 0.5 (animator speed 1.2 / "angry" bool
     * are owner-side). hp/max_hp map to role_attribute fields (see .cpp note).
     */
    void OnHurt(int hpAfter, int maxHp);

    /**
     * @brief Dizzy: latch the stun. Gated on !dead(0x38).
     *
     * FAITHFUL: BossAI13__Dizzy @ game_full.c:958520. When !dead, writes
     * dizzy(0xa1)=1 (animator "walk"=false is owner-side). No RNG.
     * @return true if the gate passed and dizzy was latched.
     */
    bool OnDizzy();

    /**
     * @brief OnGameStateChange: wake on room-ready.
     *
     * FAITHFUL: BossAI13__OnGameStateChange @ game_full.c:958299. When
     * game_state==1 and the maker's room reports ready, writes awake(0x18)=1 and
     * vtable-dispatches StartBossAI (owner-side; jumptable at 0x00adc9f8 not
     * recovered). No RNG.
     * @param gameState the broadcast state.
     * @param roomReady the_maker.the_room ready flag (offset chain 0x25->0x28->0x10).
     * @return true if the boss woke this call.
     */
    bool OnGameStateChange(int gameState, bool roomReady);

    /**
     * @brief InAtk01 bullet count selector: 7 while angry, else 5.
     * FAITHFUL: BossAI13__InAtk01 @ game_full.c:958330 (uVar2 = 5; if angry 7).
     * The RGWeapon/RGBullet Instantiate is owner-side; only the count is logic.
     */
    int Atk01BulletCount() const {
        return m_Angry ? kAtk01BulletsAngry : kAtk01BulletsCalm;
    }

    /**
     * @brief InAtk02 fire gate: only spawns when !dizzy.
     * FAITHFUL: BossAI13__InAtk02 @ game_full.c:958367. No RNG.
     * @return true if a bullet would spawn (gate passed).
     */
    bool Atk02WouldFire() const { return !m_Dizzy; }

    /**
     * @brief InAtk03 prefab selector: bullet03_angry(0xc0) while angry, else
     *        bullet03(0xbc). FAITHFUL: BossAI13__InAtk03 @ game_full.c:958431.
     * @return true to use the angry prefab.
     */
    bool Atk03UsesAngryPrefab() const { return m_Angry; }

    RGRandom &Rng() { return m_Rng; }

private:
    RGRandom m_Rng{};

    // Gate/state mirrors (decoded against RGEController.cs offset table). Only
    // those the decomp READS as gates or WRITES as state are present here.
    bool m_Awake = false;     ///< awake     @0x18
    bool m_Dead = false;      ///< dead      @0x38
    bool m_Dizzy = false;     ///< dizzy     @0xa1
    bool m_Shooting = false;  ///< shooting  @0x80
    bool m_CanShoot = false;  ///< can_shoot @0x40
    bool m_Angry = false;     ///< angry     @0xec (BossAI13-specific field)
    bool m_HasTarget = false; ///< target_obj@0x7c != null
    float m_ShootCd = 0.0F;   ///< shoot_cd  @0x3c
};

} // namespace Game

#endif /* GAME_BOSS_AI13_HPP */
