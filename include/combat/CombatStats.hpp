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

    // ------------------------------------------------------------------
    // Deepen (Wave I): RoleAttribute / RoleAttributePlayer regen + speed.
    //
    // The full game's RoleAttributePlayer runs three per-frame reload
    // tickers (ArmorReload / EnergyReLoad / SkillReload) driven by
    // Time.deltaTime accumulators. Only the recoverable SCALAR state and
    // cadence logic are modelled here; the UI/network side-effects
    // (UICanvas, is_local_player gating) are owner concerns and excluded.
    // None of these mechanics consume an RGRandom draw.
    // ------------------------------------------------------------------

    /// Additive movement speed (RoleAttribute.speed, +0x14). Buffs/debuffs
    /// from ChangeSpeed add onto this and SpeedBack subtracts back out.
    float speed = 0.0F;
    /// RoleAttribute.speed_rate multiplier field (declared @ +offset after
    /// speed). Stored verbatim; the decomp consults it outside the regen path.
    float speedRate = 0.0F;
    /// The last speed delta applied by ChangeSpeed (speed_change_value, +0x2c);
    /// SpeedBack subtracts exactly this back out.
    float speedChangeValue = 0.0F;

    /// Armor-reload accumulator in seconds (RoleAttributePlayer.armor_time, +0x68).
    float armorTime = 0.0F;
    /// Base armor-reload delay (RoleAttributePlayer.armor_load, +0x60). Data-driven
    /// per character; not written by the recovered ctor.
    float armorLoad = 0.0F; // TODO[verify] numeric default set by data, not in decomp body
    /// Extra armor-reload delay (RoleAttributePlayer.armor_rate, +0x64). Data-driven.
    float armorRate = 0.0F; // TODO[verify] numeric default set by data, not in decomp body

    /// Energy-reload accumulator in seconds (RoleAttributePlayer.energy_time, +0x6c).
    float energyTime = 0.0F;

    /**
     * @brief Energy regen cadence: +1 energy every 2.0 real seconds.
     *
     * FAITHFUL: RoleAttributePlayer__EnergyReLoad @ game_full.c:432415 -- the
     * threshold is an immediate 2.0f compare (2.0 <= energy_time), fully
     * recovered (no DAT_/data indirection on the cadence itself).
     */
    static constexpr float kEnergyReloadInterval = 2.0F;
    /// Energy gained per completed reload tick (EnergyReLoad: get_energy + 1).
    static constexpr int kEnergyReloadStep = 1;
    /// Armor gained per completed reload tick (ArmorReload: armor + 1).
    static constexpr int kArmorReloadStep = 1;

    /// Build a stat block from a character definition.
    static CombatStats FromCharacter(const CharacterDef &c) {
        CombatStats s;
        s.maxHp = c.maxHp;
        s.hp = c.hp > 0 ? c.hp : c.maxHp;
        s.maxArmor = c.maxArmor;
        s.armor = c.armor;
        s.maxEnergy = c.maxEnergy;
        s.energy = c.energy;
        s.speed = c.speed;          // RoleAttribute.speed (+0x14)
        s.speedRate = c.speedRate;  // RoleAttribute.speed_rate
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
     * FAITHFUL: FUN_005c984c @ game_full.c:471692 (armor soak) ->
     *           RGController__HurtHp @ game_full.c:471725 (rva 0x5C9954).
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

    // ------------------------------------------------------------------
    // Regen tickers (RoleAttributePlayer). Each is the recovered SCALAR core
    // of a per-frame InvokeRepeating/Update reload handler; the UI + network
    // gates around them are the owner's. None consume an RGRandom draw.
    // ------------------------------------------------------------------

    /**
     * @brief Restore HP, mirroring RoleAttribute__RestoreHealth's clamp rule.
     *
     * FAITHFUL: RoleAttribute__RestoreHealth @ game_full.c:432288:
     *   hp += amount;                       // unconditional add
     *   if (amount < 0 && hp < 1) hp = 0;   // ONLY the damage path floors at 0
     * The positive (heal) branch in the decomp falls through to a UICanvas pop-up
     * (owner side) and does NOT clamp to max_hp here, so we faithfully do not cap
     * the upper bound in this method (that is @ref Heal's separate contract).
     *
     * @param amount signed delta (negative = damage, positive = heal).
     */
    void RestoreHealth(int amount) {
        hp += amount;                 // unconditional, exactly as the decomp
        if (amount < 0 && hp < 1) {   // damage path floors at 0; heal path does not
            hp = 0;
        }
    }

    /**
     * @brief Restore armor, mirroring RoleAttributePlayer__RestoreArmor.
     *
     * FAITHFUL: RoleAttributePlayer__RestoreArmor @ game_full.c:432491:
     *   if (amount == 0) return;            // no-op on zero (and the original also
     *                                       //   gates on is_local_player == 1)
     *   armor += amount;
     *   if (amount < 1) { if (armor < 0) armor = 0; }      // damage: floor at 0
     *   else            { if (armor > max_armor) armor = max_armor; } // gain: cap
     * The is_local_player gate and the UICanvas/Prefab effects are owner concerns;
     * the recovered scalar clamp logic is reproduced exactly.
     *
     * @param amount signed delta (negative = chip armor, positive = restore).
     */
    void RestoreArmor(int amount) {
        if (amount == 0) {
            return;
        }
        armor += amount;
        if (amount < 1) {
            if (armor < 0) {
                armor = 0;
            }
        } else if (armor > maxArmor) {
            armor = maxArmor;
        }
    }

    /**
     * @brief One armor-reload tick (RoleAttributePlayer__ArmorReload).
     *
     * FAITHFUL: RoleAttributePlayer__ArmorReload @ game_full.c:432354:
     *   if (armor < max_armor) {
     *       armor_time += deltaTime;
     *       if (armor_load + armor_rate <= armor_time) {
     *           armor += 1;
     *           armor_time = armor_load;   // reset to BASE delay, not 0
     *       }
     *   }
     * Armor regenerates one point at a time once @ref armorTime reaches the
     * (data-driven) @ref armorLoad + @ref armorRate threshold; on a completed
     * tick the accumulator resets to @ref armorLoad (NOT zero -- carrying the base
     * delay forward), faithful to the decomp.
     *
     * @param deltaTime frame time in seconds (Time.deltaTime in the original).
     * @return true iff this tick granted a point of armor.
     */
    bool ArmorReloadTick(float deltaTime) {
        if (armor >= maxArmor) {
            return false;
        }
        armorTime += deltaTime;
        if (armorLoad + armorRate <= armorTime) {
            armor += kArmorReloadStep;
            armorTime = armorLoad; // reset to base delay (decomp: = armor_load)
            return true;
        }
        return false;
    }

    /**
     * @brief One energy-reload tick (RoleAttributePlayer__EnergyReLoad).
     *
     * FAITHFUL: RoleAttributePlayer__EnergyReLoad @ game_full.c:432415:
     *   if (energy < max_energy) {
     *       energy_time += deltaTime;
     *       if (2.0 <= energy_time) {
     *           energy = energy + 1;       // via get_energy/set_energy vtable
     *           energy_time = 0;           // reset accumulator to zero
     *       }
     *   }
     * The +1 step and the hardcoded 2.0s cadence (@ref kEnergyReloadInterval) are
     * fully recovered immediates. Unlike armor, the accumulator resets to 0.
     *
     * @param deltaTime frame time in seconds.
     * @return true iff this tick granted a point of energy.
     */
    bool EnergyReloadTick(float deltaTime) {
        if (energy >= maxEnergy) {
            return false;
        }
        energyTime += deltaTime;
        if (kEnergyReloadInterval <= energyTime) {
            energy += kEnergyReloadStep;
            energyTime = 0.0F; // decomp resets to 0 (not to the threshold)
            return true;
        }
        return false;
    }

    /**
     * @brief Apply an additive speed change (RoleAttribute__ChangeSpeed core).
     *
     * FAITHFUL: RoleAttribute__ChangeSpeed @ game_full.c:432166 (and the
     * RoleAttribute__SpeedBack @ game_full.c:432274 inverse):
     *   speed_change_value = value;   // +0x2c, remembered for the revert
     *   speed += value;               // +0x14, additive buff/debuff
     * The string-id bookkeeping and the Resources.Load buff VFX / Invoke timer are
     * owner concerns; the recovered scalar is the additive delta + its memo so
     * @ref SpeedBack can subtract exactly the same value out again.
     *
     * @param value signed speed delta (e.g. a slow debuff is negative).
     */
    void ChangeSpeed(float value) {
        speedChangeValue = value; // memo for SpeedBack (decomp: +0x2c)
        speed += value;           // additive (decomp: +0x14 += value)
    }

    /**
     * @brief Revert the last @ref ChangeSpeed (RoleAttribute__SpeedBack core).
     *
     * FAITHFUL: RoleAttribute__SpeedBack @ game_full.c:432274:
     *   speed -= speed_change_value;   // +0x14 -= +0x2c
     * (The decomp also resets the speed-change id to the "none" literal; that
     * string bookkeeping is owner-side.)
     */
    void SpeedBack() {
        speed -= speedChangeValue; // exact inverse of the last ChangeSpeed
    }
};

/**
 * @struct WeaponDefaultStats
 * @brief The hardcoded RGWeapon constructor stat table (player weapon base).
 *
 * FAITHFUL: RGWeapon___ctor @ game_full.c:431736. These are the inline default
 * field values a freshly-constructed RGWeapon carries before any prefab/data
 * overrides them. Offsets map to the RoleAttribute/RGWeapon field order in the
 * exported RGWeapon layout; the float immediates are decoded IEEE-754.
 *
 *   +0x14 = 1   (byte)  -> activate          = true
 *   +0x1c = 0x14        -> atk               = 20
 *   +0x20 = 1           -> through_count slot = 1
 *   +0x24 = 0x40400000  -> repel             = 3.0
 *   +0x28 = 0x41c00000  -> bullet_speed      = 24.0
 *   +0x30 = 5           -> deviation         = 5
 *   +0x34 = 1           -> consume           = 1
 *   +0x40 = 0x3f800000  -> weapon_speed      = 1.0
 *   +0x44 = 0x3dcccccd  -> atk_move_speed    = 0.1
 *   +0x58 = 1, +0x5c = 1 (bytes) -> two armed/usable bool flags
 */
struct WeaponDefaultStats {
    static constexpr int kAtk = 20;              ///< +0x1c = 0x14.
    static constexpr int kThroughCount = 1;      ///< +0x20 = 1.
    static constexpr float kRepel = 3.0F;        ///< +0x24 = 0x40400000.
    static constexpr float kBulletSpeed = 24.0F; ///< +0x28 = 0x41c00000.
    static constexpr int kDeviation = 5;         ///< +0x30 = 5.
    static constexpr int kConsume = 1;           ///< +0x34 = 1.
    static constexpr float kWeaponSpeed = 1.0F;  ///< +0x40 = 0x3f800000.
    static constexpr float kAtkMoveSpeed = 0.1F; ///< +0x44 = 0x3dcccccd.
    static constexpr bool kActivate = true;      ///< +0x14 = 1.
};

/**
 * @struct EnemyWeaponDefaultStats
 * @brief The hardcoded RGEWeapon constructor stat table (enemy weapon base).
 *
 * FAITHFUL: RGEWeapon___ctor @ game_full.c:474299. The enemy weapon has no
 * item/audio fields, so its layout is tighter than RGWeapon's:
 *
 *   +0x10 = 1   (byte)  -> activate      = true
 *   +0x14 = 1           -> through_count slot = 1
 *   +0x18 = 0x40400000  -> repel         = 3.0
 *   +0x20 = 0x41c00000  -> bullet_speed  = 24.0
 *   +0x28 = 5           -> deviation     = 5
 *   +0x2d = 1   (byte)  -> an armed/usable bool flag
 *   +0x30 = 1           -> consume / through slot = 1
 *
 * Note the enemy ctor does NOT set atk, weapon_speed, or atk_move_speed inline
 * (those default-construct to 0 and are data-filled), so they are intentionally
 * absent here -- modelling them would be fabrication.
 */
struct EnemyWeaponDefaultStats {
    static constexpr int kThroughCount = 1;      ///< +0x14 = 1.
    static constexpr float kRepel = 3.0F;        ///< +0x18 = 0x40400000.
    static constexpr float kBulletSpeed = 24.0F; ///< +0x20 = 0x41c00000.
    static constexpr int kDeviation = 5;         ///< +0x28 = 5.
    static constexpr int kConsume = 1;           ///< +0x30 = 1.
    static constexpr bool kActivate = true;      ///< +0x10 = 1.
};

} // namespace Game

#endif /* GAME_COMBAT_STATS_HPP */
