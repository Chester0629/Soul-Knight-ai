#include "world/ItemWishingWell.hpp"

namespace Game {

bool ItemWishingWell::Trigger() {
    // FAITHFUL: ItemWishingWell__Triggerable @ game_full.c:257329
    // iVar1 = *(param_1 + 0x3c) + 1; *(param_1 + 0x3c) = iVar1; if (iVar1 == 2)
    // { ...owner success path... }  return 0;
    // The owner success path (RGGameProcess host gate + get_Inst) is owner-side;
    // we return only the gate boolean. NO RNG.
    m_TriggerCount += 1;
    return m_TriggerCount == kTriggerThreshold;
}

ItemWishingWell::FailPhase ItemWishingWell::ClassifyFail() const {
    // FAITHFUL: ItemWishingWell__OnItemTriggerFail @ game_full.c:257387
    // if (*(param_1 + 0x3c) != 2) { if (*(param_1 + 0x3c) == 1) { transform } }
    // else { UICanvas__GetInstance }.  We model ONLY which branch is taken; the
    // UICanvas / transform actions are owner-side. NO RNG, no state write.
    if (m_TriggerCount == kTriggerThreshold) {
        return FailPhase::Armed; // counter ==2 -> owner UI canvas.
    }
    if (m_TriggerCount == 1) {
        return FailPhase::Charging; // counter ==1 -> owner transform poke.
    }
    return FailPhase::Idle; // any other counter -> fail is a no-op.
}

ItemWishingWell::Step ItemWishingWell::NextStep(int incomingState) {
    // FAITHFUL: ItemWishingWell_<>c__Iterator0__MoveNext @ game_full.c:257579
    // iVar2 = state@0x14; state@0x14 = -1; iVar1 = 0;
    //   if (iVar2 == 1) iVar1 = 4;   if (iVar2 == 0) iVar1 = 3;
    //   if (iVar1 == 4) { OpenChest(...); }            // state 1 -> Open
    //   else if (iVar1 == 3) { yield WaitForSeconds }  // state 0 -> Wait
    // The selector intermediates (4/3) are coroutine bookkeeping; we collapse
    // the equivalent (state 1 -> Open, state 0 -> Wait, else Done). NO RNG.
    if (incomingState == 1) {
        return Step::Open;
    }
    if (incomingState == 0) {
        return Step::Wait;
    }
    return Step::Done;
}

int ItemWishingWell::OpenChest(int poolSize) {
    // FAITHFUL: ItemWishingWell__OpenChest @ game_full.c:257407
    // if (*(param_1 + 0x14) == 0) assert;  // rg_random must be non-null
    // RGRandom__Range(rg_random, 0, *(param_1 + 0x38), 0);
    // The single draw is Range(0, pool_size) - one int draw, max EXCLUSIVE.
    // pool_size (field 0x38) is owner-set prefab data; passed in here.
    //
    // The decomp ALWAYS calls Range(0, pool_size); we call it unconditionally so
    // the stream matches draw-for-draw. RGRandom's own degenerate guard makes
    // Range(0, n<=0) return 0 WITHOUT advancing the stream (matching the
    // original's empty-range behaviour); we do not special-case it here.
    return m_Rng.Range(0, poolSize); // exactly one int draw, in order.
}

} // namespace Game
