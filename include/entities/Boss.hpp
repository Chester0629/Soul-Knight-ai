#ifndef GAME_BOSS_HPP
#define GAME_BOSS_HPP

#include <memory>
#include <string>

#include <glm/glm.hpp>

#include "Util/Animation.hpp"
#include "Util/Collider.hpp"
#include "Util/GameObject.hpp"

#include "combat/BossAI01.hpp"
#include "combat/CombatStats.hpp"

namespace Game {

/**
 * @class Boss
 * @brief A boss entity driven by the faithful BossAI01 phase logic.
 *
 * Chases the player, enters the angry phase at < 50% HP (halving its shoot
 * cadence via BossAI01), and fires a fan on each shoot tick. HP uses the enemy
 * rule (straight to HP, no armor). The bullet spawn itself is done by the scene
 * from @ref Think's decision.
 */
class Boss : public Util::GameObject {
public:
    /// @param spriteName sprite-set basename for this boss (e.g. "boss11"); the
    ///        frames are <spriteName>_0..N. Defaults to "boss01" (back-compat).
    Boss(const std::string &resourceRoot, glm::vec2 spawnPos, int maxHp,
         float shootCd, int seed, const std::string &spriteName = "boss01");

    /// Advance one step. @return chase direction; sets @p outShoot on a shoot
    /// tick and @p outAttack to the chosen attack index (varies the fan).
    glm::vec2 Think(float dtMs, glm::vec2 playerPos, bool &outShoot,
                    int &outAttack);

    /// Apply damage (straight to HP) and update the angry-phase trigger.
    void TakeDamage(int dmg);

    CombatStats &MutableStats() { return m_Stats; }
    const CombatStats &Stats() const { return m_Stats; }
    bool Angry() const { return m_Boss.Angry(); }
    bool IsDead() const { return m_Stats.IsDead(); }
    glm::vec2 Position() const { return m_Transform.translation; }
    Util::Collider GetCollider() const;
    float Speed() const { return m_Speed; }

    int RoomId() const { return m_RoomId; }
    void SetRoomId(int id) { m_RoomId = id; }

private:
    BossAI01 m_Boss;
    CombatStats m_Stats;
    float m_Speed{45.0F};
    float m_ShootTimerMs{0.0F};
    std::shared_ptr<Util::Animation> m_Anim;
    int m_RoomId{-1};
};

} // namespace Game

#endif /* GAME_BOSS_HPP */
