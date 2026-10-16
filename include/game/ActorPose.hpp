#ifndef GAME_GAME_ACTORPOSE_HPP
#define GAME_GAME_ACTORPOSE_HPP

#include <cmath>

#include <glm/glm.hpp>

/**
 * @file ActorPose.hpp
 * @brief Slice 4 (characters): the pure maths of how a character is posed -- which way it faces, which
 *        Animator clip plays, where the held weapon sits / points and where its muzzle (gun_point) is.
 *        No GL, no globals, NaN/overflow-safe, so it is unit-tested (test/ActorPoseTest.cpp) and shared
 *        by the sim (bullet origin) and the view (weapon sprite, flip, shadow).
 *
 * FACING. FAITHFUL @ game_typed.c RGController$$set_facing 50538 / get_facing 50488: set_facing(+1) sets
 * `transform_img.rotation` to the static RotRight, set_facing(-1) to RotLeft; the whole `img` subtree (body
 * sprite, hand mounts h1/h2/back) is turned by it. RGController$$.cctor 52581 (DISASM 0x5cd7f8..0x5cd86c) builds
 * RotRight = Quaternion.Euler(0, 0, 0) and RotLeft = Quaternion.Euler with 180.0f (0x43340000) in the THIRD
 * argument register. UNVERIFIED: which Euler axis that slot is -- a z-180 would turn the sprite upside down,
 * which no ground-truth frame shows; every captured left-facing frame (z_knight_aim.png 0360 / 1332 / 1764) is
 * the plain x-MIRROR of the right-facing pose (scarf and visor face swap sides). The port therefore follows the
 * observed result: a left-facing hero is the x-mirror of the right-facing one, about the root's vertical axis.
 * Which side a hero faces comes from RGController.AutoLock (game_typed.c 51330..51480): with a target the facing
 * follows the target's side, otherwise Sign(movementInput.dir.x). The port has a mouse aim instead of a target
 * lock, so the aim.x side is the target side (PORT CHOICE T-69).
 *
 * WEAPON. The weapon prefab is a child of the hand mount h1 (c01.prefab: (0, 0.5), under `img`); its
 * sprite child `w` has its pivot at the grip (weapons_19: pivot (0, 0.35)) and the muzzle child
 * `gun_point` sits at (0.6, 0.15) in the weapon's frame. In the hero's mirrored frame (facing f) the
 * gun points along the aim angle a, so a weapon-local vector l = (lx, ly) is at
 *     R(a) * (lx, f * ly)                     (the vertical component flips with the facing)
 * from the hand: a left-facing sprite is drawn mirrored in x (ground truth 1764: aiming left the grip
 * hangs BELOW the barrel, it is not turned upside down), and the gun_point keeps its "top of the
 * barrel" side. The same formula places the bullet spawn (Gun001$$Attack DISASM 0x3eb124: the bullet is
 * taken at gun_point.position, not at the body).
 */
namespace Game::ActorPose {

/// Which Animator clip family an actor plays (the controller's `run` bool / `dead` trigger).
enum class ActorClip { Idle = 0, Run = 1, Dead = 2 };

/// PORT CHOICE (T-70): an actor counts as "running" above this speed (px/s). The original sets the
/// Animator `run` bool from a non-zero move direction; the port only sees positions, so a sub-pixel
/// per-second drift (depenetration jitter) must not flip the clip.
inline constexpr float kRunSpeedEpsPxPerSec = 1.0F;

/// PORT CHOICE (T-69): |aim.x| at or below this keeps the previous facing (an exactly vertical aim has
/// no side; the ground truth keeps the last side at a straight-down aim, crops 2088 / 2196).
inline constexpr float kFacingDeadZone = 1.0e-3F;

/// Animator state selection: dead beats run beats idle (controller: ide <-> run on the bool, any -> dead).
inline ActorClip SelectClip(bool dead, bool moving) {
    if (dead) {
        return ActorClip::Dead;
    }
    return moving ? ActorClip::Run : ActorClip::Idle;
}

/// True when moving by @p displacementPx over @p dtMs is a run (finite, positive dt, finite speed above the
/// epsilon). A non-finite displacement, a non-positive dt or an overflowing speed is "not moving".
inline bool IsMoving(glm::vec2 displacementPx, float dtMs) {
    if (!std::isfinite(displacementPx.x) || !std::isfinite(displacementPx.y) || !std::isfinite(dtMs) ||
        dtMs <= 0.0F) {
        return false;
    }
    const float speed = std::sqrt(displacementPx.x * displacementPx.x + displacementPx.y * displacementPx.y) *
                        1000.0F / dtMs;
    // A displacement / dt that overflows float is a teleport, not a run.
    return std::isfinite(speed) && speed > kRunSpeedEpsPxPerSec;
}

/// RGController.set_facing value: +1 right / -1 left. @p dx is the x of the aim (or of the movement /
/// target direction for enemies); a non-finite or dead-zone x keeps @p previous. @p previous is
/// normalised to +-1 (anything else counts as +1).
inline int ResolveFacing(int previous, float dx) {
    const int prev = previous < 0 ? -1 : 1;
    if (!std::isfinite(dx) || std::fabs(dx) <= kFacingDeadZone) {
        return prev;
    }
    return dx > 0.0F ? 1 : -1;
}

/// Angle (radians, atan2) of a direction; 0 for a zero or non-finite vector.
inline float AimAngleRad(glm::vec2 dir) {
    if (!std::isfinite(dir.x) || !std::isfinite(dir.y) || (dir.x == 0.0F && dir.y == 0.0F)) {
        return 0.0F;
    }
    return std::atan2(dir.y, dir.x);
}

/// A weapon-local vector @p local (px, x = along the gun) as seen from the hand when the gun points
/// along @p aim and the hero faces @p facing: R(aim) * (lx, facing * ly). A zero / non-finite aim is
/// the +x axis.
inline glm::vec2 WeaponLocalToWorld(glm::vec2 local, glm::vec2 aim, int facing) {
    const float a = AimAngleRad(aim);
    const float c = std::cos(a);
    const float s = std::sin(a);
    const float ly = (facing < 0 ? -1.0F : 1.0F) * local.y;
    return {c * local.x - s * ly, s * local.x + c * ly};
}

/// World position of the hand mount h1 given the body centre @p position. @p handRootPx is h1 in the
/// prefab-root space (root = the feet, +y up) and @p feetOffsetPx is Position() - feet, so the hand is
/// at feet + (facing * hx, hy): the `img` subtree mirrors about the root's vertical axis.
inline glm::vec2 HandWorld(glm::vec2 position, glm::vec2 handRootPx, glm::vec2 feetOffsetPx, int facing) {
    const float f = facing < 0 ? -1.0F : 1.0F;
    return position + glm::vec2{f * handRootPx.x - feetOffsetPx.x, handRootPx.y - feetOffsetPx.y};
}

/// World position of a point @p localPx of the weapon frame (the gun_point muzzle, or the sprite
/// offset): hand + R(aim) * (lx, facing * ly).
inline glm::vec2 WeaponPointWorld(glm::vec2 handWorld, glm::vec2 localPx, glm::vec2 aim, int facing) {
    return handWorld + WeaponLocalToWorld(localPx, aim, facing);
}

/// How a held-weapon sprite is drawn by Util::GameObject (model = T * R(rot) * S(scale) about the
/// pivot): scale.x = +-1 times the draw scale (the x mirror of a left-facing hero) and the rotation
/// that makes the sprite's +x point along @p aim after that mirror. Right: rot = a. Left: the sprite is
/// mirrored, so rot = a + pi (normalised to [-pi, pi)) -- R(a+pi) * (-lx, ly) == R(a) * (lx, -ly).
struct SpritePose {
    float rotationRad = 0.0F;
    float scaleSignX = 1.0F;
};
inline SpritePose WeaponSpritePose(glm::vec2 aim, int facing) {
    SpritePose p;
    const float a = AimAngleRad(aim);
    constexpr float kPi = 3.14159265358979323846F;
    if (facing < 0) {
        p.scaleSignX = -1.0F;
        float r = a + kPi;
        if (r >= kPi) {
            r -= 2.0F * kPi;
        }
        p.rotationRad = r;
    } else {
        p.rotationRad = a;
    }
    return p;
}

/// PORT CHOICE (T-69): the x that decides an enemy / boss facing (feed it to ResolveFacing). An awake hostile
/// (the hero is in its room: @p targetInRoom) faces its TARGET -- the hero's side, target.x - self.x; a sleeping
/// or out-of-room one faces its HEADING (the sim's move direction, which keeps its last value while still).
/// The original's per-species facing rule is in truncated EnemyAI bodies (UNVERIFIED); RGController.AutoLock
/// (the hero: target side when it has a target, else Sign(move dir x)) is the only recovered analogue.
inline float EnemyFaceDx(bool targetInRoom, glm::vec2 selfPos, glm::vec2 targetPos, glm::vec2 heading) {
    return targetInRoom ? targetPos.x - selfPos.x : heading.x;
}

/// PORT CHOICE (T-71): the lock (red ring) range. The original shows the red `shadow_lock` ring on the one
/// enemy the hero's auto-aim holds (RGController$$AutoLock 005cade4 -> RGEController$$BeLock 005cb664 /
/// OffLock 005cb62c; the FindTarget search radius and layer mask are in a truncated body); the ground-truth
/// capture shows it on an enemy 2.3 cells away (fight1/f0468) and on none of the enemies ~9+ cells away
/// (fight1/f0630). The port has no auto-aim, so the ring marks the nearest living hostile within this
/// range of the hero. UNVERIFIED value.
inline constexpr float kLockRangePx = 7.0F * 32.0F;

/// Index (0..count-1) of the nearest hostile to @p from within @p rangePx (inclusive), or -1. @p posOf(i)
/// is the position and @p aliveOf(i) the liveness of candidate i; a dead or non-finite candidate, a
/// non-finite @p from or a non-positive / non-finite range selects nothing. Ties go to the lowest index
/// (deterministic).
template <class PosFn, class AliveFn>
inline int PickLockTarget(int count, glm::vec2 from, float rangePx, PosFn posOf, AliveFn aliveOf) {
    if (count <= 0 || !std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(rangePx) ||
        rangePx <= 0.0F) {
        return -1;
    }
    int best = -1;
    float bestSq = 0.0F;
    const float rangeSq = rangePx * rangePx;
    for (int i = 0; i < count; ++i) {
        if (!aliveOf(i)) {
            continue;
        }
        const glm::vec2 p = posOf(i);
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            continue;
        }
        const float dx = p.x - from.x;
        const float dy = p.y - from.y;
        const float dSq = dx * dx + dy * dy;
        if (!std::isfinite(dSq) || dSq > rangeSq) {
            continue;
        }
        if (best < 0 || dSq < bestSq) {
            best = i;
            bestSq = dSq;
        }
    }
    return best;
}

} // namespace Game::ActorPose

#endif /* GAME_GAME_ACTORPOSE_HPP */
