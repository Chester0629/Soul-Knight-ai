#include "combat/BossAI10.hpp"

#include <glm/glm.hpp>

namespace Game {

// FAITHFUL: BossAI10__RunReflection @ game_full.c:955988.
// Two ordered float draws Range(-1f, 1f) (hex 0xbf800000 / 0x3f800000), x then y,
// fed to Vector2(x, y).normalized -> set_move_direction. The target_obj position
// read and animator SetBool("run") are owner-side and take NO draw.
glm::vec2 BossAI10::RunReflectionDirection() {
    const float x = m_Rng.Range(kDirMin, kDirMax); // first draw
    const float y = m_Rng.Range(kDirMin, kDirMax); // second draw
    const glm::vec2 v{x, y};
    const float len = glm::length(v);
    if (len <= 0.0F) {
        return glm::vec2{0.0F, 0.0F}; // degenerate -> Unity normalized is (0,0)
    }
    return v / len;
}

// FAITHFUL: BossAI10__ShootReflection @ game_full.c:956046.
// Gate: can_shoot(0x40) && !dead(0x38) && !dizzy(0xA1). The single int draw
// Range(0, 100) (max exclusive) happens ONLY inside the gate; the gated-out path
// takes NO draw (stream lockstep). The decomp writes NO field: it reads the
// gates, takes the one draw, then dispatches via the vtable jumptable
// (*param_1 + 0x10c) to StartAtkNN -- there is no stored attack index. The
// roll->StartAtkNN jumptable was not recovered (indirect jump 0x00ad05f4); 5
// equal buckets are a reconstruction. We return the bucket but store nothing.
int BossAI10::ShootReflectionChooseAttack() {
    if (!(m_CanShoot && !m_Dead && !m_Dizzy)) {
        return kNoAttack; // gated out: no draw, no field written
    }
    const int roll = m_Rng.Range(0, kRollCeiling);
    int idx = roll / (kRollCeiling / kAttackCount) + 1; // 1-based Atk01..Atk05
    if (idx > kAttackCount) {
        idx = kAttackCount;
    }
    return idx; // reconstructed bucket only; decomp stores no atk_index
}

// FAITHFUL: BossAI10__GetHurt @ game_full.c:956221 -> BossAI10__BossAngry @ 956294.
// Gate: awake(0x18) && !dead(0x38). After RGEController::GetHurt applies the
// damage to role_attribute (+0x1C hp / +0x18 max_hp), enter angry once when
// hp/max_hp < 0.5 (field 0xCC). BossAngry sets angry=1 and halves shoot_cd
// (0x3C); animator set_speed(1.2f) and SetBool("angry") are owner-side.
// NOTE: the decomp's heal+clamp (hp += 4 then "if max_hp < hp ... hp = max_hp")
// is NOT unconditional - it is gated by Animator.GetBool("atk03")==1 and is
// owner-side animator state the brain does not model; the ratio test uses the
// post-damage hp/max_hp directly.
float BossAI10::OnHurt(int hpAfter, int maxHp, float shootCd) {
    if (!m_Awake || m_Dead) {
        return shootCd; // gated out
    }
    if (maxHp <= 0) {
        return shootCd; // guard divide-by-zero (not reachable with valid vitals)
    }
    const float frac =
        static_cast<float>(hpAfter) / static_cast<float>(maxHp);
    if (frac < kAngryHpFraction && !m_Angry) {
        m_Angry = true;                  // BossAngry: angry(0xCC) = 1
        shootCd *= kAngryShootCdScale;   // BossAngry: shoot_cd(0x3C) *= 0.5
    }
    return shootCd;
}

// FAITHFUL: BossAI10__CreateIcicle @ game_full.c:956547.
// ++atk_4_count (byte 0xD0 = param_1[0x34]); cap = 3, or 4 when angry
// (byte 0xCC = (char)param_1[0x33]). Below the cap the original
// Invoke("CreateIcicle", atk_4_count * 0.25f); at/after the cap it ends the
// attack (vtable end slot). Icicle Instantiate + RGMusicManager.PlayEffect are
// owner-side and take NO draw.
float BossAI10::CreateIcicleStep() {
    m_Atk4Count = m_Atk4Count + 1;
    const int cap = m_Angry ? kIcicleCapHit : kIcicleCap;
    if (m_Atk4Count < cap) {
        return static_cast<float>(m_Atk4Count) * kIcicleInvokeStep; // re-Invoke delay
    }
    return -1.0F; // chain ends: owner runs the end-attack slot
}

// FAITHFUL: BossAI10__Dizzy @ game_full.c:956625.
// Only when !dead(0x38) latch dizzy(0xA1)=1; animator SetBool("run", false) owner-side.
bool BossAI10::ApplyDizzy() {
    if (m_Dead) {
        return false;
    }
    m_Dizzy = true;
    return true;
}

// FAITHFUL: BossAI10__OnGameStateChange @ game_full.c:956420.
// gameState != 1 -> early return. Else when the room-ready flag (the_maker.the_room
// +0x10 == 1) holds, set awake(0x18)=1; animator atk03 SetBool(false) owner-side.
bool BossAI10::OnGameStateChange(int gameState, bool roomReady) {
    if (gameState != 1) {
        return false;
    }
    if (!roomReady) {
        return false;
    }
    m_Awake = true;
    return true;
}

} // namespace Game
