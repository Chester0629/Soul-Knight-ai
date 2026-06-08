#include "combat/BossAI12Parent.hpp"

namespace Game {

// FAITHFUL: BossAI12Parent__UpDateBossHp @ game_full.c:957447.
//   iVar2 = ai1.role_attribute (param_1+0x18 ->+0x70)
//   iVar4 = ai2.role_attribute (param_1+0x1c ->+0x70)
//   BossInfo__UpDateBossHp(boss_info,
//       *(iVar4+0x1c) + *(iVar2+0x1c),   // current = ai2.hp + ai1.hp
//       *(iVar1+0x18) + *(iVar3+0x18));  // max     = ai2.max_hp + ai1.max_hp
// The summation order (ai2 first) does not affect the int result; we keep ai1
// first for readability. No RNG draw. The bar fill is then derived inside
// BossInfo__UpDateBossHp @ game_full.c:956318 as (current * 400) / max.
float BossAI12Parent::UpDateBossHp(int ai1Hp, int ai1MaxHp, int ai2Hp,
                                   int ai2MaxHp) {
    const int totalHp = TotalHp(ai1Hp, ai2Hp);
    const int totalMaxHp = TotalMaxHp(ai1MaxHp, ai2MaxHp);
    m_LastTotalHp = totalHp;
    m_LastTotalMaxHp = totalMaxHp;

    // BossInfo__UpDateBossHp: sizeDelta.x = (current * 400.0f) / max.
    // Guard the divide (max == 0 cannot fill a bar); the original asserts a
    // live UI Image so max is never 0 in practice, but we stay total here.
    float width = 0.0F;
    if (totalMaxHp != 0) {
        width = (static_cast<float>(totalHp) * kHpBarFullWidth) /
                static_cast<float>(totalMaxHp);
    }
    m_LastHpBarWidth = width;
    return width;
}

// FAITHFUL: BossAI12Parent__BossDead @ game_full.c:957553.
//   if (*(char*)(boss_ai_1 + 0x38) == 0) return;   // half 1 still alive
//   if (*(char*)(boss_ai_2 + 0x38) == 0) return;   // half 2 still alive
//   ... Singleton<PlayerSaveData>.Inst tail (owner-side score/save) ...
// Modelled as the pure both-dead gate; the recoverable head is exactly these
// two dead-latch (0x38) checks. No field write, no RNG draw. The truncated tail
// (PlayerSaveData singleton) is owner-side and intentionally not modelled.
// (BossDead is a static predicate; see header.)

} // namespace Game
