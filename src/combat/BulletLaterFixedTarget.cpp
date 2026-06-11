#include "combat/BulletLaterFixedTarget.hpp"

namespace Game {

// FAITHFUL: BulletLaterFixedTarget__FindTarget @ game_full.c:963433-963434.
//   fVar3 = *(float *)(param_1 + 0x44);
//   uVar1 = UnityEngine_Random__Range(0, fVar3, fVar3 + fVar3, 0);
// The Range min is the raw delay field (fVar3). The draw itself is the engine
// global RNG (UnityEngine.Random), not RGRandom -- only the bound is recovered.
float BulletLaterFixedTarget::InvokeDelayMin(float delay) { return delay; }

// FAITHFUL: BulletLaterFixedTarget__FindTarget @ game_full.c:963434. The Range
// max argument is the literal `fVar3 + fVar3` (delay + delay); reproduced as an
// add rather than a `2 *` multiply to stay byte-faithful to the decomp.
float BulletLaterFixedTarget::InvokeDelayMax(float delay) { return delay + delay; }

// FAITHFUL: BulletLaterFixedTarget__FindTarget @ game_full.c:963434,:963435. The
// scheduled Invoke delay is UnityEngine.Random.Range(min, max) = min + t*(max-min)
// for the engine RNG sample t in [0,1]. The sample is the engine global stream's
// (owner-supplied here; no RGRandom draw is made). The subsequent
// MonoBehaviour.Invoke(StringLiteral_6654, delay), the Rigidbody2D.velocity =
// Vector2.zero brake, and the `*(byte*)(this + 0x50) = 0` flag clear are all
// owner-side (get_transform/Rigidbody/Invoke) and are not modelled.
float BulletLaterFixedTarget::ScheduledDelay(float delay, float t) {
    const float min = InvokeDelayMin(delay);
    const float max = InvokeDelayMax(delay);
    return min + t * (max - min);
}

} // namespace Game
