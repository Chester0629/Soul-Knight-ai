#include "combat/CharSkillC07.hpp"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
constexpr float kTimerEpsilonSec = 1.0e-4F;
constexpr float kPi = 3.14159265358979F;
constexpr float kDegToRad = kPi / 180.0F;
constexpr float kRadToDeg = 180.0F / kPi;

float Len(glm::vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

// Vector3.normalized: zero below 1e-5.
glm::vec2 Normalized(glm::vec2 v) {
    const float l = Len(v);
    return l > 1.0e-5F ? v / l : glm::vec2{0.0F, 0.0F};
}

// Vector2.Angle: UNSIGNED degrees in [0, 180]; 0 when either vector is ~zero.
float UnsignedAngleDeg(glm::vec2 a, glm::vec2 b) {
    const float denom = std::sqrt((a.x * a.x + a.y * a.y) * (b.x * b.x + b.y * b.y));
    if (denom < 1.0e-15F) {
        return 0.0F;
    }
    const float c = std::min(1.0F, std::max(-1.0F, (a.x * b.x + a.y * b.y) / denom));
    return std::acos(c) * kRadToDeg;
}
} // namespace

// ---------------------------------------------------------------------------
// BatSwarm
// ---------------------------------------------------------------------------

// RoleSkill's spawn loop: DISASM 0x266b28-0x266d2c. The loop index runs -n..n-1 over
// 2n bats; move_angle = (r6 / n) with r6 = -180*n then += 180 per bat (__aeabi_idiv
// 0x1ceef4, a signed integer division), i.e. -180 + 180*k/n for k = 0..2n-1; the first
// n bats (loop index < 0) get localScale.x = -1 (@0x266cb8-0x266d18). Start (DISASM
// 0xaf2950-0xaf2a4c): velocity = speed * scale.x * (cosf(a), sinf(a)) with `a` used
// directly as RADIANS.
void BatSwarm::Launch(int count, glm::vec2 origin, int damage, float speed) {
    const int n = count / 2;
    for (int k = 0; k < count; ++k) {
        Bat b;
        b.pos = origin;
        b.damage = damage;
        b.speed = speed;
        b.moveAngle = static_cast<float>((-180 * n + 180 * k) / (n > 0 ? n : 1));
        const float sx = k < n ? -1.0F : 1.0F;
        b.vel = {speed * sx * std::cos(b.moveAngle), speed * sx * std::sin(b.moveAngle)};
        b.findTimer = kFindTargetDelaySec;
        m_Bats.push_back(b);
    }
}

// FindTarget (DISASM 0xaf37a0-0xaf3bbc): nearest hostile within 12.0 of the bat (best
// seeded 100.0, strict `<`) -> target + state 1; none -> the player + state 2.
void BatSwarm::FindTarget(Bat &b, const std::vector<SkillTarget> &hostiles) const {
    const SkillTarget *found = nullptr;
    float best = kBestDistInit;
    for (const SkillTarget &h : hostiles) {
        const float d = Len(h.pos - b.pos);
        if (d > kFindRadiusUnits || !(d < best)) {
            continue;
        }
        best = d;
        found = &h;
    }
    if (found != nullptr) {
        b.target = found->id;
        b.state = 1;
    } else {
        b.target = kPlayerTarget;
        b.state = 2;
    }
}

void BatSwarm::Step(float dtSec, glm::vec2 playerPos, const std::vector<SkillTarget> &hostiles,
                    std::vector<Hit> &hits, std::vector<bool> &backs) {
    auto targetPos = [&](const Bat &b, glm::vec2 &out) {
        if (b.target == kPlayerTarget) {
            out = playerPos;
            return true;
        }
        for (const SkillTarget &h : hostiles) {
            if (h.id == b.target) {
                out = h.pos;
                return true;
            }
        }
        return false; // the target is gone (dead / asleep).
    };
    auto doHit = [&](Bat &b) {
        // GetHurt(damage, source) on the target; has_hit = true; state = 2; target = the
        // player (DISASM 0xaf2fe4-0xaf3718). Knockback / hit effect NOT PORTED (T-45).
        hits.push_back({b.target, b.damage});
        b.hasHit = true;
        b.state = 2;
        b.target = kPlayerTarget;
    };

    std::vector<Bat> keep;
    keep.reserve(m_Bats.size());
    for (Bat &b : m_Bats) {
        // Invoke("FindTarget", 0.5f), fired once (Unity script time, before FixedUpdate).
        if (!b.findFired) {
            b.findTimer -= dtSec;
            if (b.findTimer <= kTimerEpsilonSec) {
                b.findFired = true;
                FindTarget(b, hostiles);
            }
        }
        bool arrived = false;
        glm::vec2 tp{0.0F, 0.0F};
        switch (b.state) {
        case 1: {
            if (!targetPos(b, tp)) {
                FindTarget(b, hostiles); // target hp <= 0 -> FindTarget (DISASM 0xaf2b68).
                break;
            }
            const glm::vec2 desired = Normalized(tp - b.pos);
            const glm::vec2 velDir = Normalized(b.vel);
            const float angle = UnsignedAngleDeg(desired, velDir);
            if (std::fabs(angle) > kAimToleranceDeg) {
                b.moveAngle += angle * kSteerGain;
            } else {
                b.state = 3; // DISASM 0xaf3464.
            }
            const float rad = b.moveAngle * kDegToRad;
            b.vel = {b.speed * std::cos(rad), b.speed * std::sin(rad)};
            if (Len(b.pos - tp) < kHitRangeHoming && !b.hasHit) {
                doHit(b);
            }
            break;
        }
        case 3: {
            if (!targetPos(b, tp)) {
                // PORT CHOICE (T-45): the original would dereference a destroyed
                // Transform; the port ends the dash and sends the bat home, no hit.
                b.state = 2;
                b.target = kPlayerTarget;
                break;
            }
            b.vel = Normalized(tp - b.pos) * (b.speed * kDashSpeedMul);
            if (Len(b.pos - tp) < kHitRangeDash && !b.hasHit) {
                doHit(b);
            }
            break;
        }
        case 2: {
            b.vel = Normalized(playerPos - b.pos) * (b.speed * kDashSpeedMul);
            if (Len(b.pos - playerPos) < kArriveRange) {
                arrived = true;
            }
            break;
        }
        default:
            break; // state 0: free flight at the Start velocity.
        }
        if (arrived) {
            backs.push_back(b.hasHit); // BatBack(has_hit) then Destroy(bat).
            continue;
        }
        b.pos += b.vel * dtSec; // Rigidbody2D integration.
        keep.push_back(b);
    }
    m_Bats.swap(keep);
}

// ---------------------------------------------------------------------------
// CharSkillC07
// ---------------------------------------------------------------------------

CharSkillC07::CharSkillC07(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// RoleSkill gate: TYPED 1969-1997. Entry: DISASM 0x266ab4-0x266ac0 (bat_count = 0,
// bat_hit_count = 0, in_skill = 1; reduce_skill_cd untouched), the loop count 2n from
// 0x266ae4 (n = 3, or 5 when strengthened).
bool CharSkillC07::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false;
    }
    m_BatCount = 0;
    m_BatHitCount = 0;
    m_InSkill = true;
    m_LaunchPending = 2 * (m_Strengthen ? kPairsStrengthened : kPairsNormal);
    return true;
}

int CharSkillC07::ConsumeLaunch() {
    const int n = m_LaunchPending;
    m_LaunchPending = 0;
    return n;
}

// BatBack: TYPED 2058-2118 + DISASM 0x2670ec-0x267264 (see the header audit).
CharSkillC07::BatBackResult CharSkillC07::BatBack(bool hasHit, int hp, int maxHp) {
    BatBackResult r;
    ++m_BatCount; // TYPED 2073.
    if (hasHit) {
        ++m_BatHitCount;
        const int need = m_BuffVampireHp ? kHitsPerHealBuffed : kHitsPerHeal;
        if (m_BatHitCount >= need) {
            if (hp < maxHp) {
                r.healHp = kHealAmount;
            } else {
                r.restoreEnergy = kEnergyRestore;
            }
            m_BatHitCount = 0;
        }
    } else {
        // TYPED 2085-2097 (the !has_hit path, DISASM 0x2670ec bne 0x267178): reduce_skill_cd += skill_cd / (strengthen ? 10.0 : 6.0).
        m_ReduceSkillCd +=
            m_SkillCd / (m_Strengthen ? kRefundDivisorStrengthened : kRefundDivisorNormal);
    }
    const int total = 2 * (m_Strengthen ? kPairsStrengthened : kPairsNormal);
    if (m_BatCount == total) { // TYPED 2098-2113 / DISASM 0x267284-0x2672a0.
        EndSkill();
        r.skillEnded = true;
    }
    return r;
}

// KillSomeOne: DISASM 0x266e08-0x26706c.
CharSkillC07::KillResult CharSkillC07::KillSomeOne(int hp, int maxHp) {
    KillResult r;
    if (m_BuffVampireHp && hp < maxHp) {
        if (m_Rng.Range(0, 100) <= kKillHealRollMax) {
            r.healHp = kHealAmount;
        }
    }
    if (m_BuffEnergy) {
        r.restoreEnergy = kKillEnergyRestore;
    }
    return r;
}

// RoleSkillEnd: TYPED 2004-2026.
void CharSkillC07::EndSkill() {
    m_InSkill = false;
    m_ThisSkillTime = 0.0F; // ReSetSkillReload
    // SkillReload(reduce_skill_cd): count up by the banked reduction, clamped to skill_cd.
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + m_ReduceSkillCd, m_SkillCd);
    }
    m_ReduceSkillCd = 0.0F;
}

// Update: TYPED 1937-1946 -> SkillReload(dt), unconditional.
void CharSkillC07::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dtMs / kMsPerSecond, m_SkillCd);
    }
}

float CharSkillC07::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

} // namespace Game
