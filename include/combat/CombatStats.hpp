#ifndef GAME_COMBAT_STATS_HPP
#define GAME_COMBAT_STATS_HPP

#include <algorithm>

#include "data/GameData.hpp"

namespace Game {
/**
 * @struct CombatStats
 * @brief Combat vitals shared by the player and enemies.
 *
 * Soul-Knight rules: incoming damage is absorbed by @ref armor first and only
 * overflows into @ref hp once armor is depleted. Armor regenerates in the full
 * game (after a delay); HP does not. Energy fuels weapon fire and active skills.
 *
 * Pure value type - no engine or rendering dependencies, fully unit-testable.
 */
struct CombatStats {
    int hp = 0;
    int maxHp = 0;
    int armor = 0;
    int maxArmor = 0;
    int energy = 0;
    int maxEnergy = 0;

    /// Build a stat block from a character definition.
    static CombatStats FromCharacter(const CharacterDef &c) {
        CombatStats s;
        s.maxHp = c.maxHp;
        s.hp = c.hp > 0 ? c.hp : c.maxHp;
        s.maxArmor = c.maxArmor;
        s.armor = c.armor;
        s.maxEnergy = c.maxEnergy;
        s.energy = c.energy;
        return s;
    }

    /// Apply damage: armor absorbs first, the overflow hits HP. Both floor at 0.
    void TakeDamage(int dmg) {
        if (dmg <= 0) {
            return;
        }
        const int overflow = dmg - armor;
        armor = std::max(0, armor - dmg);
        if (overflow > 0) {
            hp = std::max(0, hp - overflow);
        }
    }

    /// Restore HP up to @ref maxHp.
    void Heal(int amount) {
        if (amount > 0) {
            hp = std::min(maxHp, hp + amount);
        }
    }

    /// Restore armor up to @ref maxArmor.
    void AddArmor(int amount) {
        if (amount > 0) {
            armor = std::min(maxArmor, armor + amount);
        }
    }

    /// Spend energy; returns false (and changes nothing) if there isn't enough.
    bool SpendEnergy(int amount) {
        if (amount <= 0) {
            return true;
        }
        if (energy < amount) {
            return false;
        }
        energy -= amount;
        return true;
    }

    /// Restore energy up to @ref maxEnergy.
    void AddEnergy(int amount) {
        if (amount > 0) {
            energy = std::min(maxEnergy, energy + amount);
        }
    }

    bool IsDead() const { return hp <= 0; }

    /**
     * @brief Player receive-damage chain (armor -> hp), reporting death.
     *
     * FAITHFUL: RGController__HurtArmor (FUN_005c9810 @ rva 0x5B9810) ->
     *           RGController__HurtHp @ game_full.c:471725 (rva 0x5B9954).
     *
     * Reproduces the recovered player path exactly:
     *   - armor soaks the hit 1:1 (RestoreArmor(-dmg), clamped >= 0);
     *   - only the part exceeding armor spills into HP (RestoreHealth(-overflow),
     *     clamped >= 0);
     *   - death is "hp reached 0" (hp <= 0), not "hp went negative".
     * Behaviourally identical to @ref TakeDamage; this overload simply returns the
     * post-resolution death state so callers can drive the Dead() transition.
     *
     * @return true iff this stat block is dead (hp <= 0) after the hit.
     */
    bool ApplyPlayerDamage(int dmg) {
        TakeDamage(dmg);
        return IsDead();
    }

    /**
     * @brief Enemy receive-damage chain: straight HP subtraction, NO armor.
     *
     * FAITHFUL: RGEController__SyncGetHurt @ game_full.c:473331 (rva 0x5C2AD8):
     *   role_attribute.hp -= damage   (no clamp, no armor mitigation).
     * Death is decided by the caller via @ref IsDead (hp <= 0). The original does
     * NOT clamp the subtraction here, so HP may legitimately go negative before
     * the hp<=0 death check fires; we preserve that (no std::max).
     *
     * @return true iff this stat block is dead (hp <= 0) after the hit.
     */
    bool ApplyEnemyDamage(int dmg) {
        hp -= dmg; // straight HP loss, exactly as SyncGetHurt (no armor, no clamp)
        return IsDead();
    }
};
} // namespace Game

#endif /* GAME_COMBAT_STATS_HPP */
