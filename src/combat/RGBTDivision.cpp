#include "combat/RGBTDivision.hpp"

namespace Game {

// FAITHFUL: RGBTDivision__OnTriggerEnter2D @ game_full.c:468941.
// The recovered body (lines 468950-468966):
//   RGBulletTrigger__OnTriggerEnter2D(param_1, param_2);     // base dispatch (owner)
//   if (CompareTag(param_2, <tagA>) == 0) {                   // owner
//       if (CompareTag(param_2, <tagB>) != 1) return;         // owner
//   }
//   *(undefined1 *)(param_1 + 100) = 1;                       // destroyed (0x64) = 1
// The tag checks and base call are owner concerns; the pure part is the
// conditional flag set: when a recognised tag matched, latch `destroyed`.
void RGBTDivision::OnTriggerEnter2D(bool tagMatched) {
    if (tagMatched) {
        m_Destroyed = true;
    }
}

// FAITHFUL: RGBTDivision__Division @ game_full.c:469085. The decomp gate is
// `if (-*(int *)(param_1 + 0x4c) < *(int *)(param_1 + 0x4c))`, i.e. `-count <
// count`, which for a content count is exactly `count > 0`. Reproduced literally
// to preserve the recovered branch.
bool RGBTDivision::HasDivisionCount(int count) {
    return -count < count;
}

// FAITHFUL: RGBTDivision__Division @ game_full.c:469078, 469085.
//   if (*(char *)(param_1 + 100) == '\0') {     // !destroyed
//       PlayEffect(audio_clip);                  // owner side effect (omitted)
//       if (-count < count) {                    // count > 0
//           <PrefabPool child-bullet fan spawn>  // owner
//       }
//   }
// The fan spawn happens iff `!destroyed && count > 0`.
bool RGBTDivision::ShouldDivide(bool destroyed, int count) {
    return !destroyed && HasDivisionCount(count);
}

// FAITHFUL: RGBTDivision__Division @ game_full.c:469078, 469085 (instance form).
bool RGBTDivision::ShouldDivide(int count) const {
    return ShouldDivide(m_Destroyed, count);
}

} // namespace Game
