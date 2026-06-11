#include "combat/BossAI14.hpp"

#include <glm/glm.hpp>

// OWNER-side bodies (no brain logic; named here for the faithful inventory, all
// take NO RNG draw):
//   BossAI14__Awake/Start    @958672/958685 - get_gameObject/get_transform stubs
//   BossAI14__FixedUpdate    @958698 - Rigidbody2D velocity integration (move_dir
//                                      * speed * mods + force), gated awake(0x18)
//                                      && !shooting... ; pure physics, no draw
//   BossAI14__StartAtk01..06 @958980+ - Animator triggers/bools + RGMusicManager
//   BossAI14__FixedRotation  @959156 - aim transform toward target_obj(0x7c)
//   BossAI14__Dead           @959217 - base Dead + Animator "dead" bool
//   BossAI14__CreateTransferGate @959237 - BossInfo.HideHpBar + Instantiate gate
//   BossAI14__OnGameStateChange  @959271 - sets awake(0x18)=1 on room-ready=1;
//                                          vtable tail (no draw)
//   BossAI14__InAtk01/02/03/04   @959302+ - bullet Instantiate / PlayEffect
//   BossAI14__Atk02BulletTimer/Move @959396/959411 - coroutine iterators
//   BossAI14__ShowMiddleFoot @959495 - Debug.Log + GetComponent<SpriteRenderer>
//   BossAI14__GetAngle       @959538 - returns fixed_angle(0x84) or target dir
//   BossAI14_<>c__Iterator1__MoveNext @959571 - Atk02 bullet-move coroutine
//   BossAI14_<>c__Iterator2__MoveNext @959689 - StartCoroutine-scheduled iterator,
//                                      truncated tail; owner-side, NO modelled draw
//   BossAI14_<>c__Iterator3__MoveNext @959790 - StartCoroutine-scheduled iterator,
//                                      truncated tail; owner-side. NOTE: this
//                                      iterator carries an ADDITIONAL
//                                      RGRandom::Range(0,100) int draw (@959863)
//                                      that the owner-side coroutine port MUST
//                                      reproduce to keep the per-instance RNG
//                                      stream in lockstep (max EXCLUSIVE).

namespace Game {

// FAITHFUL: BossAI14___ctor @ game_full.c:958662.
//   *(undefined2 *)(param_1 + 0xe5) = 0x101;  // root(0xe5)=1, can_hit(0xe6)=1
// (little-endian 16-bit store seeds the two adjacent bool latches), then chains
// RGEController___ctor(param_1, 0). The base ctor is owner-side; we only model
// the two field writes.
void BossAI14::Construct() {
    m_Root = true;   // 0xe5 low byte
    m_CanHit = true; // 0xe6 high byte
}

// FAITHFUL: BossAI14__Scout @ game_full.c:958811.
//   cVar1 = *(p+0xa1);                  // dizzy
//   bVar2 = (dizzy == 0);
//   if (bVar2) cVar1 = *(p+0x38);       // short-circuit: read dead only if !dizzy
//   if (!dizzy && !dead) { *(p+0x7c)=0; get_transform... }   // clear target_obj
// The transform read + target re-scan is owner-side; we model the gate result.
// No RNG draw on either path.
bool BossAI14::ScoutClearsTarget() const {
    return !m_Dizzy && !m_Dead;
}

// FAITHFUL: BossAI14__RunReflection @ game_full.c:958838 (root branch).
//   if (*(p+0xe5) != 0) {                                   // root
//     set_move_direction(zero);                             // owner-side
//     uVar1 = RGRandom__Range(rng, scout_rate*0.5, scout_rate);
//     Invoke("RunReflection", uVar1); return;               // owner-side
//   }
// One float draw; max INCLUSIVE. Returns the scheduled delay.
float BossAI14::RunReflectionRootDelay(float scoutRate) {
    return m_Rng.Range(scoutRate * kRootDelayLoFactor, scoutRate);
}

// FAITHFUL: BossAI14__RunReflection @ game_full.c:958838 (else branch).
//   // (if target_obj(0x7c) exists, get_position -> owner-side, no draw)
//   uVar1 = RGRandom__Range(rng, 0xbf800000, 0x3f800000);   // -1f .. 1f  (x)
//   uVar3 = RGRandom__Range(rng, 0xbf800000, 0x3f800000);   // -1f .. 1f  (y)
//   FUN_00fa16ec(&v, uVar1, uVar3);                         // Vector2(x, y)
//   FUN_00fa1e04(&dir, &v);                                 // normalized
//   set_move_direction(dir);                                // owner-side
//   Animator.SetBool("walk", true);                         // owner-side
// Two float draws in x-then-y order; max INCLUSIVE.
glm::vec2 BossAI14::RunReflectionHeading() {
    const float x = m_Rng.Range(kHeadingMin, kHeadingMax);
    const float y = m_Rng.Range(kHeadingMin, kHeadingMax);
    const glm::vec2 v(x, y);
    const float len = glm::length(v);
    if (len <= 0.0F) {
        return glm::vec2(0.0F, 0.0F); // Vector2.normalized of zero == zero (Unity)
    }
    return v / len;
}

// FAITHFUL: BossAI14__ShootReflection @ game_full.c:958947.
//   if (*(p+0x40) != 0) {                       // can_shoot gate
//     bVar2 = (*(p+0x38) == 0);                 // dead
//     if (bVar2) cVar1 = *(p+0xa1);             // dizzy (read only if !dead)
//     if (!dead && !dizzy) {
//       if (rng != 0) RGRandom__Range(rng, 0, 100);   // single attack roll
//     }
//   }
//   (**vtable[0x10c])(...);                     // indirect jumptable: roll->StartAtkNN
// The roll->StartAtkNN jumptable was NOT recovered (indirect jump at 0x00adfc50,
// "Too many branches"). The 6 equal buckets here are a reconstruction; the single
// Range(0,100) draw keeps the stream in lockstep regardless of bucketing. When
// the gate is closed, NO draw is taken (stream lockstep). The decomp dispatches
// the roll through the unrecovered vtable[0x10c] jumptable and writes NO index
// field (BossAI14 has no atk_index member), so the bucket is returned as a PURE
// VALUE only -- modelling a stored index would be an invented field write (rule
// #2, the BossAI02 pilot bug).
int BossAI14::ShootReflectionChoose() {
    if (!m_CanShoot || m_Dead || m_Dizzy) {
        return kNoAttack; // gated out: no draw
    }
    const int roll = m_Rng.Range(0, kRollCeiling);
    int idx = roll / (kRollCeiling / kAttackCount) + 1;
    if (idx > kAttackCount) {
        idx = kAttackCount; // roll == 96..99 lands in the top bucket
    }
    return idx;
}

// FAITHFUL: BossAI14__BossAngry @ game_full.c:959191.
//   *(p+0xe4) = 1;                              // angry latch
//   *(float*)(p+0x3c) = *(float*)(p+0x3c) * 0.5;// shoot_cd halved
//   Animator.set_speed(0x3f99999a /*1.2f*/);    // owner-side
//   Animator.SetBool("angry", true);            // owner-side
// The decomp has no re-entry guard; the owner calls it once (single hp-threshold
// crossing). We latch so a defensive double call does not halve shoot_cd twice.
float BossAI14::BossAngry(float shootCd) {
    if (m_Angry) {
        return shootCd; // already enraged: do not re-halve
    }
    m_Angry = true;
    return shootCd * kAngryShootCdScale;
}

// FAITHFUL: BossAI14__Dizzy @ game_full.c:959472.
//   if (*(p+0x38) == 0) {                       // gate: not dead
//     *(p+0xa1) = 1;                            // dizzy latch
//     Animator.SetBool("walk", false);          // owner-side
//   }
// Returns true when the dizzy latch was newly raised. A second call with dizzy
// already set still passes the !dead gate but the owner schedules nothing new;
// we report no-change so the stream/state stays in lockstep.
bool BossAI14::ApplyDizzy() {
    if (m_Dead || m_Dizzy) {
        return false;
    }
    m_Dizzy = true;
    return true;
}

// FAITHFUL: BossAI14__EndAtk03 @ game_full.c:959455.
//   Animator.SetBool("atk03", false);           // owner-side
// The decomp ONLY lowers the "atk03" animator bool; it does NOT write field 0xed.
// The inAtk03(0xed) latch that GetForce gates on is owned/toggled by the
// InAtk03/Attacking03 coroutine iterators (set=1 at coroutine start, cleared at
// end), NOT by EndAtk03. So this body is a pure owner-side no-op stub here.
void BossAI14::EndAtk03() {
    // owner-side: Animator.SetBool("atk03", false). No brain field write, no draw.
}

// FAITHFUL: BossAI14__GetForce @ game_full.c:959519.
//   bVar2 = (*(p+0xed) == 0);                   // inAtk03
//   if (bVar2) cVar1 = *(p+0xe5);               // root (read only if !inAtk03)
//   if (!inAtk03 && !root) RGEController__GetForce();  // base knockback
// No RNG draw; the base GetForce (impulse store + 28.0 cap) is owner-side.
bool BossAI14::AcceptsForce() const {
    return !m_InAtk03 && !m_Root;
}

} // namespace Game
