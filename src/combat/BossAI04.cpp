#include "combat/BossAI04.hpp"

namespace Game {

// FAITHFUL: BossAI04.GetHurt -> BossAngry (field 0xf9; hp/max_hp < 0.5, once).
void BossAI04::OnHurt(int hpAfter, int maxHp) {
    if (m_Angry || maxHp <= 0) {
        return;
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction) {
        m_Angry = true; // BossAngry(): angry = true (+ animator "angry" bool)
    }
}

// FAITHFUL: BossAI04.ShootReflection -> rg_random.Range(0, 100) attack roll.
int BossAI04::ChooseAttack() {
    const int roll = m_Rng.Range(0, kRollCeiling);
    // Original roll->StartAtkNN jumptable was not recovered (indirect jump at
    // 0x542b64); 5 equal buckets are a reconstruction (manual_flags). atk_index
    // is 1-based (Atk01..Atk05) per StartAtkNN / StopAttack.
    int idx = roll / (kRollCeiling / kAttackCount) + 1;
    if (idx > kAttackCount) {
        idx = kAttackCount;
    }
    m_AtkIndex = idx;
    return idx;
}

// FAITHFUL: BossAI04.StopAttack switch -> atk_index = 0.
void BossAI04::StopAttack() {
    m_AtkIndex = kNoAttack;
}

// FAITHFUL: BossAI04.StartAtk02 -> Range(2f, 4f); delay += 1f while angry.
float BossAI04::Atk02RefireDelay() {
    float delay = m_Rng.Range(kAtk02DelayMin, kAtk02DelayMax);
    if (m_Angry) {
        delay += kAngryAtk02DelayBonus; // longer windup in phase 2
    }
    return delay;
}

} // namespace Game
