#include "combat/CharSkillC03.hpp"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {
constexpr float kMsPerSecond = 1000.0F;
constexpr float kTimerEpsilonSec = 1.0e-4F; // float-drift guard for the Invoke countdown.

float Dist(glm::vec2 a, glm::vec2 b) {
    const glm::vec2 d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y);
}
} // namespace

// ---------------------------------------------------------------------------
// ThunderChain
// ---------------------------------------------------------------------------

// StartThunder (DISASM 0xaffe6c): intensify (+0x14) = value1; has_hit_list (+0x2c) = new
// List; atk (+0x24) = value3; last_target (+0x28) = value2; the thunder object sits at
// the first target (CreateTunder sets its transform.position @0x264b3c); has_hit_list
// .Add(first) @0xb002c4; Invoke(FindNextTarget, 0.15f) @0xb00344.
void ThunderChain::Start(const SkillTarget &first, int atk, bool intensify) {
    m_Done = false;
    m_Intensify = intensify;
    m_Atk = atk;
    m_Count = 0;
    m_Origin = first.pos;
    m_Last = first;
    m_HitList.clear();
    m_HitList.push_back(first.id);
    m_Timer = kHopIntervalSec;
}

// FindNextTarget (DISASM 0xafef08-0xaffdd0). See the header for the branch map.
bool ThunderChain::Step(float dtSec, const std::vector<SkillTarget> &hostiles,
                        std::vector<Hit> &hits) {
    if (m_Done) {
        return false;
    }
    m_Timer -= dtSec;
    if (m_Timer > kTimerEpsilonSec) {
        return true; // Invoke not yet due.
    }

    // CircleCastAll(this.position, radius 10.0, layer "Body_E") -> nearest hostile (to
    // the thunder object, best seeded 100.0) that is not already in has_hit_list.
    const SkillTarget *found = nullptr;
    float best = kBestDistInit;
    for (const SkillTarget &h : hostiles) {
        const float d = Dist(h.pos, m_Origin);
        if (d > kRadiusUnits || !(d < best)) {
            continue;
        }
        if (std::find(m_HitList.begin(), m_HitList.end(), h.id) != m_HitList.end()) {
            continue; // has_hit_list.Contains(candidate)
        }
        best = d;
        found = &h;
    }

    if (found != nullptr && m_Count <= kCountGate) {
        // Hop (DISASM 0xaff600): count++ BEFORE the hit; damage = intensify ?
        // atk + 2*count : atk (DISASM 0xaffab8 / 0xaffaf0); has_hit_list.Add; the
        // chain re-arms Invoke(FindNextTarget, 0.15) (DISASM 0xaffdcc).
        ++m_Count;
        const int damage = m_Intensify ? m_Atk + 2 * m_Count : m_Atk;
        hits.push_back({found->id, damage});
        m_HitList.push_back(found->id);
        m_Last = *found;
        m_Timer = kHopIntervalSec;
        return true;
    }

    // No next target (or count > 3). Only a chain that never hopped (count == 0) still
    // acts: it re-strikes last_target for atk * 2 (DISASM 0xaff3ec-0xaff448,
    // `lsl r1, r4, #1`); a chain that already hopped simply ends (DISASM 0xaff3f4
    // `bne 0xaffdd0`). The re-strike only lands on a target that still exists.
    if (m_Count == 0 && found == nullptr) {
        for (const SkillTarget &h : hostiles) {
            if (h.id == m_Last.id) {
                hits.push_back({h.id, m_Atk * 2});
                break;
            }
        }
    }
    m_Done = true;
    return false;
}

// ---------------------------------------------------------------------------
// CharSkillC03
// ---------------------------------------------------------------------------

CharSkillC03::CharSkillC03(float skillCd)
    : m_SkillCd(skillCd < 0.0F ? 0.0F : skillCd),
      m_ThisSkillTime(skillCd < 0.0F ? 0.0F : skillCd) {}

// RoleSkill gate: TYPED 872-901 (awake caller-side).
// The entry tail (DISASM 0x264468-0x2644b4): in_skill = 1; Invoke("CreateTunder", 0.4f).
bool CharSkillC03::TryActivateSkill() {
    if (m_InSkill || !SkillReady()) {
        return false;
    }
    m_InSkill = true;
    m_TimerArmed = true;
    m_InvokeLeftMs = kCreateTunderDelaySec * kMsPerSecond;
    return true;
}

// CreateTunder (DISASM 0x26451c-0x264c14): spawn only when a target was found (and the
// game is in state 1, always true here); RoleSkillEnd runs either way.
CharSkillC03::TunderDecision CharSkillC03::CreateTunder(bool targetFound) {
    TunderDecision d;
    if (targetFound) {
        d.strike = true;
        d.damage = kStrikeDamage;
    }
    m_TimerArmed = false;
    m_InvokeLeftMs = 0.0F;
    EndSkill(); // vcall slot 25 @0x264bf4 -- after the optional spawn, unconditionally.
    return d;
}

// RoleSkillEnd (DISASM 0x264c50-0x264ce4): in_skill = 0 (strb @0x264cb8); ReSetSkillReload
// (this_skill_time = 0, @0x264cd4); UpdateShadowLock tail (tint -> T-51). Unconditional,
// like every other hero's visible RoleSkillEnd body (C05 1584-1638, C07 2004-2026,
// C11 3587-3601): no "already out of skill" guard.
void CharSkillC03::EndSkill() {
    m_InSkill = false;
    m_ThisSkillTime = 0.0F;
}

// DeadChild (TYPED 943-956): in_skill = false; role_attribute.this_skill_time = 0.0.
void CharSkillC03::DeadChild() {
    m_InSkill = false;
    m_ThisSkillTime = 0.0F;
    m_TimerArmed = false;
    m_InvokeLeftMs = 0.0F; // a dead hero's pending Invoke is moot (PORT: part of T-50).
}

// Update (TYPED 841-850): AttributeUpdate -> SkillReload(dt), unconditional (even
// in_skill). The armed Invoke counts down alongside (Unity script time).
void CharSkillC03::Tick(float dtMs) {
    if (dtMs <= 0.0F) {
        return;
    }
    if (m_ThisSkillTime < m_SkillCd) {
        m_ThisSkillTime = std::min(m_ThisSkillTime + dtMs / kMsPerSecond, m_SkillCd);
    }
    if (m_TimerArmed && m_InvokeLeftMs > 0.0F) {
        m_InvokeLeftMs -= dtMs;
    }
}

float CharSkillC03::CooldownRemaining() const {
    const float remaining = m_SkillCd - m_ThisSkillTime;
    return remaining > 0.0F ? remaining : 0.0F;
}

// CreateTunder's scan (DISASM 0x2646ac-0x2649f8): iterate the CircleCastAll hits; for
// each, dist = Distance(hit, player); only `dist < best` (best seeded 100.0) candidates
// are Linecast-tested (from (hit.x, hit.y+0.5) to (player.x, player.y+0.5), layer
// "Wall"); an unblocked one becomes the new best.
int CharSkillC03::PickNearestTarget(const std::vector<SkillTarget> &hostiles, glm::vec2 origin,
                                    const SkillLineBlockedFn &blocked) {
    int pick = -1;
    float best = kSearchBestInit;
    for (std::size_t i = 0; i < hostiles.size(); ++i) {
        const float d = Dist(hostiles[i].pos, origin);
        if (d > kSearchRadiusUnits || !(d < best)) {
            continue;
        }
        if (blocked && blocked(hostiles[i].pos + glm::vec2{0.0F, kLosOffsetY},
                               origin + glm::vec2{0.0F, kLosOffsetY})) {
            continue;
        }
        best = d;
        pick = static_cast<int>(i);
    }
    return pick;
}

} // namespace Game
