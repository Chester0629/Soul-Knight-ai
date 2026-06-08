#include "world/RGBox.hpp"

namespace Game {

// FAITHFUL: RGBox__SetSourceObject @ game_full.c:468730
void RGBox::SetSourceObject(int sourceObject) {
    // The original is a single field store: *(this + 0x14) = param_2. No gate,
    // no RNG, no other side effect.
    m_SourceObject = sourceObject;
}

// FAITHFUL: RGBox__Hit @ game_full.c:467578
RGBox::HitResult RGBox::Hit(int damage, bool sourceValid) {
    HitResult result;

    // Decompiled gate (467594-467595):
    //   iVar1 = Object.op_Implicit(source);            // is source a live object?
    //   if ((iVar1 == 1) && (0 < this[3]/*HP @0x0C*/)) { ... }
    // Both conditions must hold for the hit to register. A failed gate writes
    // NOTHING and returns uVar2 == 0. No random draw on either path.
    if (sourceValid && m_Hp > 0) {
        // 467596-467598: param_2 = this[3] - param_2; uVar2 = 1; this[3] = param_2;
        // i.e. newHp = HP - damage, mark the hit registered, then store newHp.
        const int newHp = m_Hp - damage;
        result.registered = true;
        m_Hp = newHp;

        // 467599-467601: if (newHp < 1) (**this+0xd4)(this, source, *(this+0xd8));
        // The below-zero branch fires a virtual on-death dispatch (owner-side
        // BoxDestroy / CreateItem). We do NOT model the vtable call; we surface
        // its CONDITION so the owner can fire it.
        if (newHp < 1) {
            result.shouldDestroy = true;
        }
    }

    // 467603: return uVar2; (0 when the gate failed, 1 on a registered hit).
    return result;
}

} // namespace Game
