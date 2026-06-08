#ifndef GAME_BOSS_AI_NIAN_LANTERN_HPP
#define GAME_BOSS_AI_NIAN_LANTERN_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class BossAINianLantern
 * @brief Faithful attack-lifecycle brain for the Nian boss's satellite lantern
 *        minion (a plain MonoBehaviour, NOT an RGEController).
 *
 * Per-content port. The lantern is a small explosive satellite that the Nian
 * boss arms during its lantern volley (BossAINian.InAtk05 / Attacking05). Once
 * armed via SetTarget(target), the lantern runs the compiler-generated
 * @c <Attacking>c__Iterator0 coroutine: it lights up (isAttack), charges toward
 * the target across several yield steps, detonates (Explode) unless it was
 * already spent, then resets and re-enables its collider. NOTE: the order the
 * coroutine resumes its cases in is a RECONSTRUCTION (the next-state pc writes
 * are inlined into the non-returning yield thunk and not recoverable); only the
 * per-case field writes are faithful (see MoveNext).
 *
 * STREAM-NEUTRAL: the entire class performs ZERO RGRandom draws (no
 * RGRandom::Range call appears anywhere in BossAINianLantern__* or the iterator
 * MoveNext). The lantern therefore does not advance any RNG stream; the brain
 * exposes Rng() only for interface symmetry with the sibling boss brains, and
 * the tests assert the no-draw invariant explicitly.
 *
 * MODELLED (pure logic): the coroutine MoveNext state machine and the EXACT
 * field writes it performs on the target lantern:
 *   - case 0 (arm):  isAttack (0x18) = true, exploded (0x19) = true; yield.
 *   - case 3 (det):  if (!exploded) Explode(); yield.  (the explode gate)
 *   - shared tail:   exploded (0x19) = false; enable collider; reset.
 * The Explode() body, transform moves (get_transform), the
 * Instantiate<RGWeapon>/PrefabManager prefab spawn in SetTarget, the
 * WaitForSeconds yields and the StartCoroutine launch are owner-side (Unity)
 * and carry only a naming comment, no brain logic.
 *
 * @see metadata BossAINianLantern.cs (fields: isAttack 0x18, exploded 0x19,
 *      explodeObj 0x1C, sourceObj 0x20, col 0x24).
 * @see recreation BossAINian.cs (Attacking05 arms each lantern: l.isAttack = true).
 * @see FAITHFUL: BossAINianLantern @ game_full.c:960798 (SetTarget), :960857
 *      (Start), :960870 (Explode), :960890 (Attacking launcher), :960905
 *      (<Attacking>c__Iterator0__MoveNext state machine; per-case field writes
 *      faithful, the inter-case resume order is a reconstruction -- see below).
 */
class BossAINianLantern {
public:
    /// Coroutine MoveNext step ids (the iterator's pc field at +0x30).
    /// 0 = arm, 1 = post-arm wait done, 2/4/5 = approach steps, 3 = detonate.
    /// The pc VALUES are recoverable (the switch in MoveNext); the resume ORDER
    /// between them is a reconstruction (next-state writes not recovered).
    enum class Step : int {
        Idle = -1,   ///< not yet started (iterator pc == 0xffffffff before run)
        Arm = 0,     ///< case 0: light + arm, then yield
        Detonate = 3 ///< case 3: explode unless already spent, then yield
    };

    BossAINianLantern() = default;

    /// Seed the (unused) stream; present only for brain-interface symmetry.
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }
    bool Seeded() const { return m_Rng.Seeded(); }

    /// True once SetTarget armed the coroutine (op_Implicit && isAttack == 0 gate).
    bool Armed() const { return m_Armed; }
    /// FAITHFUL field 0x18 (isAttack): set true when the coroutine's case 0 runs.
    bool IsAttack() const { return m_IsAttack; }
    /// FAITHFUL field 0x19 (exploded): true after arming, cleared in the tail.
    bool Exploded() const { return m_Exploded; }
    /// True on the frames the modelled MoveNext decided to call Explode().
    bool DidExplode() const { return m_DidExplode; }

    /**
     * @brief Arm the lantern for an attack run (BossAINianLantern.SetTarget).
     *
     * FAITHFUL: SetTarget gates the whole launch on
     * @c op_Implicit(this) && this.isAttack == 0 -- i.e. the lantern must be a
     * live object and not already attacking. Only then does it StartCoroutine the
     * Attacking iterator (and Instantiate the RGWeapon prefab, owner-side). This
     * brain models the GATE and the resulting armed state; it does not spawn the
     * weapon. Calling it on an already-attacking lantern is a no-op (returns
     * false), faithful to the @c isAttack == 0 guard.
     *
     * @param alive mirrors op_Implicit(this) (true when the GameObject is live).
     * @return true if the coroutine was newly launched, false if gated out.
     */
    bool SetTarget(bool alive);

    /**
     * @brief Advance the Attacking coroutine one MoveNext step.
     *
     * BossAINianLantern_<Attacking>c__Iterator0__MoveNext @ game_full.c:960905.
     * FAITHFUL parts: the head dispatch (read pc at +0x30, write -1, fold pc > 5
     * to the finished path) and the per-case FIELD WRITES below.
     *
     * RECONSTRUCTION (NOT recovered): every case ends in the non-returning yield
     * thunk FUN_010b7dcc, which swallows the inlined `iterator.pc = N` write for
     * the next resume. The ONLY +0x30 writes in the decomp are the entry read and
     * `*(p+0x30) = -1`, so the inter-case RESUME ORDER (and thus the total step
     * count) is unrecoverable. The ordering modelled here is a manual
     * reconstruction (matching the BossAI04 'jumptable not recovered ... is a
     * reconstruction' convention); the once-per-run detonation is a consequence
     * of that reconstructed order, not a recovered fact.
     *
     * Recoverable step semantics (decoded against the metadata field offsets):
     *   - case 0  : isAttack (0x18) = true, exploded (0x19) = true; then yield.
     *   - case 1  : break -> run the shared tail (see below).
     *   - case 2  : transform move toward target (owner); yield.
     *   - case 3  : if (exploded == false) Explode(); yield.  <- explode gate
     *   - case 4/5: transform move of the source object (owner); yield.
     *   - shared tail (reached only from case 1): exploded (0x19) = false,
     *               enable collider (owner), transform; coroutine continues.
     *   - default : coroutine finished (return 0).
     *
     * @return true if the coroutine yielded again, false if it has finished.
     */
    bool MoveNext();

    /// The current coroutine step id (the iterator pc, -1 before/after the run).
    int CurrentStep() const { return m_Pc; }

    RGRandom &Rng() { return m_Rng; }

private:
    /// Highest valid MoveNext case id; pc values above this map to "finished".
    static constexpr int kMaxStep = 5;

    RGRandom m_Rng{};   ///< stream-neutral: never drawn from (asserted in tests).
    bool m_Armed = false;
    bool m_IsAttack = false; ///< field 0x18 (isAttack).
    bool m_Exploded = false; ///< field 0x19 (exploded).
    bool m_DidExplode = false;
    int m_Pc = -1;      ///< iterator pc (+0x30); -1 == idle, advances 0..5.
};

} // namespace Game

#endif /* GAME_BOSS_AI_NIAN_LANTERN_HPP */
