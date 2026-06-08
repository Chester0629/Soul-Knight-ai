#include "combat/Gun008.hpp"

namespace Game {

// FAITHFUL: Gun008_<>c__Iterator0__MoveNext @ game_full.c:317560-317566.
// fVar6 = fVar7 = (float)owner+0x30 (base); fVar7 = fVar7 + fVar6 * (component+0x20)
// = base + base*scale = base*(1+scale). Stored at iterator+0x14. No RNG draw.
float Gun008::SweepHalfAngle(float base, float scale) {
    return base + base * scale;
}

// FAITHFUL: Gun008_<>c__Iterator0__MoveNext @ game_full.c:317569.
// RGRandom__Range(owner+0x60, -fVar7, fVar7): ONE max-inclusive float draw per
// fired tick, stored at iterator+0x18. Preserve the single draw exactly.
float Gun008::SweepScatterAngle(float half) {
    return m_Rng.Range(-half, half);
}

// FAITHFUL: Gun008_<>c__Iterator0__MoveNext @ game_full.c:317492.
// iVar2 = state(0x2c); state = -1; dispatch (state 0 -> first WaitForSeconds
// yield; state 1 -> resume: counter(0xc)++; counter < limit(0x10) ? fire+yield
// : end). The owner owns WaitForSeconds/PlayEffect/the scatter draw/bullet
// spawn; we model which step fires and which step ends.
bool Gun008SweepIterator::MoveNext() {
    const int entryState = m_State;
    m_State = kDone; // *(param+0x2c) = 0xffffffff at entry (317499).
    m_Fired = false;

    if (entryState == kStart) {
        // 317519-317527: state 0 -> yield WaitForSeconds (OWNER). Stay alive and
        // stage the resume branch for the next pump.
        m_State = kResume;
        return true;
    }

    if (entryState == kResume) {
        // 317542-317545: counter(0xc) = counter + 1.
        m_Counter += 1;
        // 317546: if (counter < limit(0x10)) -> fire one tick + yield again;
        // else fall through to end.
        if (m_Counter < m_Limit) {
            // PlayEffect + scatter draw + bullet spawn are OWNER side-effects;
            // here we only record that this pump fires and re-stage the resume
            // branch (317574 writes -1 then the continuation re-enters at 1).
            m_Fired = true;
            m_State = kResume;
            return true;
        }
        // counter >= limit -> the coroutine ends (return 0 / false).
        return false;
    }

    // kDone or any other state -> ended.
    return false;
}

} // namespace Game
