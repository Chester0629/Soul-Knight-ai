#include "combat/RGPetController.hpp"

#include <algorithm>

namespace Game {

// FAITHFUL: RGPetController__FixedUpdate @ game_full.c:427251-427312.
// Two-branch velocity state machine. Branch on decel(0x30) <= 1.0 (line 427270):
//   FOLLOW: velocity = move_direction(0x50) * speed(0x40+0x10)
//                                           * (speed_rate(0x40+0x14) + 1.0)
//           (two Vector2.op_Multiply, lines 427281-427288); decel untouched.
//   COAST : velocity = force_direction(0x34) * decel(0x30) (line 427302), then
//           the ONLY state write-back decel(0x30) *= damping(0x24) (line 427309).
// Rigidbody2D.set_velocity (427293/427308) is owner; the get_transform tail
// (427312) is owner facing. The awake gate (427266) is the caller's guard.
glm::vec2 RGPetController::FixedUpdate(float speed, float speedRate,
                                       MoveBranch &outBranch) {
    if (m_Decel <= kFollowDecelThreshold) { // line 427270
        outBranch = MoveBranch::FOLLOW;
        // move_direction * speed * (speed_rate + 1.0)  (lines 427281-427288)
        return m_MoveDirection * speed * (speedRate + 1.0F);
    }
    outBranch = MoveBranch::COAST;
    const glm::vec2 velocity = m_ForceDirection * m_Decel; // line 427302
    m_Decel = m_Decel * m_Damping;                          // line 427309 write-back
    return velocity;
}

// FAITHFUL: RGPetController__ReplyingHP @ game_full.c:427317-427364.
// Gate hp(0x1c) < max_hp(0x18) (line 427333). Accumulate the timer by deltaTime
// (line 427336). When timer >= reply_time2(0x60) + reply_time1(0x5c) (line 427338):
// heal hp += max_hp / 5 (line 427344, INTEGER division), reset timer = reply_time1
// (line 427345), then clamp hp = min(hp, max_hp) (lines 427351-427354). The
// get_gameObject "fully healed fx" tail (line 427356) is owner.
bool RGPetController::ReplyingHP(int &hp, int maxHp, float deltaTime) {
    if (hp < maxHp) {                                 // line 427333
        m_ReplyTimer += deltaTime;                    // line 427336
        if (m_ReplyTimer >= kReplyTime2 + kReplyTime1) { // line 427338
            hp = hp + maxHp / kHealDivisor;           // line 427344 (INTEGER)
            m_ReplyTimer = kReplyTime1;               // line 427345 (reset to 0x5c)
            if (hp >= maxHp) {                        // line 427351
                hp = maxHp;                           // line 427354 (clamp)
            }
            return true;
        }
    }
    return false;
}

// FAITHFUL: RGPetController__TurnTo @ game_full.c:427457-427477.
// move_direction(0x50/0x54) = Vector2.Reflect(move_direction, normal)
// (lines 427472-427476). Reflect(v, n) = v - 2*dot(v,n)*n.
void RGPetController::TurnTo(glm::vec2 normal) {
    const float d = glm::dot(m_MoveDirection, normal);
    m_MoveDirection = m_MoveDirection - 2.0F * d * normal;
}

} // namespace Game
