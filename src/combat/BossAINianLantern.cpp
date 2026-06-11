#include "combat/BossAINianLantern.hpp"

namespace Game {

// FAITHFUL: BossAINianLantern__SetTarget @ game_full.c:960798.
// Gate: op_Implicit(this) && this.isAttack (0x18) == 0  ->  StartCoroutine(Attacking).
// The Instantiate<RGWeapon>(PrefabManager.GetPrefab(7)) spawn and the coroutine
// launch itself are owner-side (Unity); we model only the gate + armed state.
bool BossAINianLantern::SetTarget(bool alive) {
    if (!alive || m_IsAttack) {
        return false; // gated out: dead object, or already attacking (isAttack != 0).
    }
    m_Armed = true;
    m_Pc = 0; // iterator armed at case 0; the next MoveNext runs the arm step.
    return true;
}

// FAITHFUL (head + per-case field writes only): BossAINianLantern_<Attacking>
//   c__Iterator0__MoveNext @ game_full.c:960905.
// Recoverable head: pc is the iterator's +0x30 field. On entry it is read into a
// local and the ONLY write to +0x30 is `*(p+0x30) = 0xffffffff` (-1); decomp:
//   uVar1 = *(uint*)(p+0x30); *(p+0x30) = 0xffffffff; if (5 < uVar1) uVar1 = -3;
// so values > 5 fold to the default (finished) path. Each case then performs its
// recoverable field writes and ends in the NON-RETURNING yield thunk
// FUN_010b7dcc, which swallows the inlined `iterator.pc = N` assignment for the
// next resume -- those next-state pc writes are therefore NOT recoverable.
//
// RECONSTRUCTION (NOT recovered): the resume ORDER between cases (0->1, tail->2,
// 2->3->4->5->finished) is a manual reconstruction -- see the per-step
// `// TODO[verify] next-state pc write not recovered (inlined into yield thunk)`
// markers below. Matching the BossAI04 convention for an unrecovered tail
// (jumptable not recovered ... is a reconstruction), the order-dependent
// behaviour (notably the once-per-run detonation) is reconstructed, not faithful.
// Only the head dispatch and the per-case field writes below are FAITHFUL.
bool BossAINianLantern::MoveNext() {
    m_DidExplode = false;

    const int pc = m_Pc;
    m_Pc = -1; // FAITHFUL: *(p+0x30) = 0xffffffff -- the only +0x30 write here.

    // FAITHFUL: if (5 < (uint)pc) pc = 0xfffffffd; -> falls to default (finished).
    if (pc < 0 || pc > kMaxStep) {
        return false; // default: return 0 (coroutine exhausted / not running).
    }

    switch (pc) {
    case 0:
        // FAITHFUL case 0: target.isAttack (0x18) = 1; target.exploded (0x19) = 1.
        m_IsAttack = true;
        m_Exploded = true;
        // TODO[verify] next-state pc write not recovered (inlined into yield thunk);
        // resume at case 1 (the tail trigger) is a reconstruction.
        m_Pc = 1;
        return true;

    case 1:
        // FAITHFUL case 1: break -> shared tail below.
        break;

    case 2:
        // case 2: transform move toward target (owner: get_transform); yield.
        // TODO[verify] next-state pc write not recovered (inlined into yield thunk).
        m_Pc = 3;
        return true;

    case 3:
        // FAITHFUL case 3: if (target.exploded (0x19) == false) Explode(target).
        if (!m_Exploded) {
            m_DidExplode = true; // Explode() (owner: PrefabPool detonation effect).
        }
        // TODO[verify] next-state pc write not recovered (inlined into yield thunk).
        m_Pc = 4;
        return true;

    case 4:
        // case 4: transform move of the source object (field +8) (owner); yield.
        // TODO[verify] next-state pc write not recovered (inlined into yield thunk).
        m_Pc = 5;
        return true;

    case 5:
        // case 5: transform move of the source object (field +8) (owner); yield.
        // TODO[verify] next-state pc write not recovered (inlined into yield thunk);
        // treating this as the final yield (-> finished) is a reconstruction.
        m_Pc = -1;
        return true;

    default:
        return false;
    }

    // FAITHFUL shared tail (reached only via case 1's break / default fallthrough):
    //   target.exploded (0x19) = 0;
    //   target.col (0x24).enabled = true;  (owner: Behaviour.set_enabled)
    //   transform (owner);  then the coroutine continues (non-returning thunk).
    m_Exploded = false;
    // TODO[verify] next-state pc write not recovered (inlined into yield thunk);
    // continuing at case 2 (the approach step) is a reconstruction.
    m_Pc = 2;
    return true;
}

// FAITHFUL: BossAINianLantern__Start @ game_full.c:960857 -- get_transform only
//   (caches originPos); owner-side, no brain logic.
// FAITHFUL: BossAINianLantern__Explode @ game_full.c:960870 -- Singleton<PrefabPool>
//   detonation spawn; owner-side, no brain logic (decision modelled in MoveNext).
// FAITHFUL: BossAINianLantern__Attacking @ game_full.c:960890 -- coroutine factory
//   (allocates <Attacking>c__Iterator0); owner-side, no brain logic.

} // namespace Game
