#include "combat/Bullet03.hpp"

namespace Game {

// FAITHFUL: Bullet03__Update reads max_time (0x4c) and max_size (0x48). These are
// owner-configured per-bullet; a_time (0x50) starts at 0. start_size is declared
// on Bullet03 but never read by Update, so it is intentionally not modelled.
void Bullet03::Configure(float maxTime, float maxSize) {
    m_MaxTime = maxTime;
    m_MaxSize = maxSize;
    m_ATime = 0.0F;
}

// FAITHFUL: Bullet03__Update @ game_full.c:962183.
//   if (a_time < max_time) {                         // 0x50 < 0x4c (strict)
//     a_time += Time.deltaTime;                      // 0x50 += dt
//     get_transform((a_time/max_time) * max_size, ...);   // OWNER localScale write
//   }
// The accumulation and the owner write happen ONLY inside the strict `<` gate; the
// timer is left to overshoot in a single step (no clamp), exactly as the decomp.
// The awake (0x20) and rotate_angle (0x1c) gates are owner state, applied by the
// caller before invoking this -- not modelled here.
bool Bullet03::Tick(float dt) {
    if (m_ATime < m_MaxTime) {
        m_ATime += dt;
        return true;
    }
    return false;
}

// FAITHFUL: Bullet03__Update @ game_full.c:962198, the `a_time / max_time` factor.
// No clamp in the original. Guard a degenerate non-positive divisor (impossible in
// a real bullet) by returning 0 rather than NaN/inf.
float Bullet03::Progress() const {
    if (m_MaxTime <= 0.0F) {
        return 0.0F;
    }
    return m_ATime / m_MaxTime;
}

// FAITHFUL: Bullet03__Update @ game_full.c:962198, the `(a_time/max_time)*max_size`
// value the owner applies to localScale.
float Bullet03::ScaledSize() const {
    return Progress() * m_MaxSize;
}

} // namespace Game
