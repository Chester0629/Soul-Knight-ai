#ifndef GAME_DATA_HPP
#define GAME_DATA_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Game {

/// Player weapon definition (from Resources/data/weapons.json).
struct WeaponDef {
    std::string id;
    int weaponType = 0;
    int atk = 0;
    float repel = 0.0F;      ///< knockback
    float bulletSpeed = 0.0F;
    int critical = 0;
    int deviation = 0;       ///< spread, in (engine) angle units
    int consume = 0;         ///< energy cost per shot
    int canThrough = 0;
    int throughCount = 0;
    float weaponSpeed = 1.0F; ///< fire-rate multiplier
    float atkMoveSpeed = 0.0F;
    int isMelee = 0;
    int needLock = 0;
    int itemLevel = 0;
    int itemValue = 0;
    /// Bullets spawned per trigger pull (subclass field; 0/absent => single shot).
    int count = 0;
    /// Fixed fan step in degrees between adjacent bullets (subclass field).
    /// RE models this as float (RGWeapon fire_angle); kept float for fidelity.
    float angle = 0.0F;
};

/**
 * @brief Energy spent per shot for @p def: `WeaponDef.consume`, floored at 1.
 *
 * The SINGLE source of the player firing-gate + energy-spend cost (`GameScene`'s
 * `m_WeaponEnergyCost`). A `consume` of 0 (or absent/negative) still costs 1 --
 * mirroring the live equip sites. This is the DERIVE rule a carried weapon must be
 * re-equipped THROUGH on a new floor (never carried as a raw int, never left at the
 * default 1): RUN_LOOP_PLAN D3. The live sim weapon path (`Sim::WeaponController`)
 * never reads `consume`; the only other reader is the scene-dead `WeaponInstance`
 * (its own test), so this cost is definitively scene-owned.
 */
inline int WeaponEnergyCost(const WeaponDef &def) {
    return def.consume > 0 ? def.consume : 1;
}

/// Bullet definition (from Resources/data/bullets.json).
struct BulletDef {
    std::string id;
    float speed = 0.0F;
    int camp = 0;            ///< 0 = player, 1 = enemy (Soul Knight convention)
    float destroyTime = 0.0F; ///< lifetime in seconds
    int rotateAngle = 0;
    int canRebound = 0;
    int manualDestroy = 0;
};

/// Enemy AI/behaviour definition (from Resources/data/enemies.json).
struct EnemyDef {
    std::string id;
    int aiLevel = 0;
    float shootCd = 0.0F;    ///< seconds between shots
    float friction = 0.0F;
    float scoutRate = 0.0F;
    int consume = 0;         ///< spawn budget cost
    int eSize = 0;
    int rewardRate = 0;
    float buffImmune = 0.0F;
    int kinematic = 0;       ///< 1 = turret-style: immovable, ignores knockback
};

/// Enemy weapon definition (from Resources/data/enemy_guns.json).
struct EnemyGunDef {
    std::string id;
    int atk = 0;
    float repel = 0.0F;
    float bulletSpeed = 0.0F;
    int deviation = 0;
    int canThrough = 0;
    int needLock = 0;
    int facing = 0;
};

/// Character/player stat block (from Resources/data/characters.json).
struct CharacterDef {
    int camp = 0;
    float speed = 0.0F;
    float speedRate = 0.0F;
    int maxHp = 0;
    int hp = 0;
    int maxArmor = 0;
    int armor = 0;
    int maxEnergy = 0;
    int energy = 0;
    int atk = 0;
    int critical = 0;
    float skillCd = 0.0F;
    float inSkillTime = 0.0F;
    float fireRate = 0.0F;
};

/// Status-effect definition (from Resources/data/buffs.json).
struct BuffDef {
    std::string id;
    int isEnemy = 0;
    float buffTime = 0.0F;
};

/// One floor-1 design room's SIZE (from Resources/data/room_layouts.json).
/// Phase 2 size source: a dungeon slot takes one of these rooms' width/height; the
/// room INTERIOR is still RoomGen-procedural (design-room layout loading is Phase 3).
struct RoomLayoutDef {
    std::string id;
    int width = 0;
    int height = 0;
    int type = 0; ///< design-room category (floor-1 rooms are all type 1).
};

/**
 * @class GameData
 * @brief Loads and indexes the generated game-data tables for the game layer.
 *
 * Reads the @c Resources/data/*.json tables produced by the offline pipeline
 * (via Util::DataStore) into typed, id-keyed definitions. Game systems query
 * these defs instead of touching JSON directly, keeping the data-driven slice
 * "add data, not engineering".
 *
 * Loading is defensive: missing files or fields are logged and substituted with
 * defaults rather than throwing, so a partial data set still yields a usable
 * (if incomplete) registry.
 */
class GameData {
public:
    /**
     * @brief Load every table from @c <resourceRoot>/data/.
     * @param resourceRoot The Resources root (e.g. the RESOURCE_DIR macro).
     * @return true if all required tables parsed as the expected shape.
     */
    bool LoadAll(const std::string &resourceRoot);

    const WeaponDef *FindWeapon(const std::string &id) const;

    /**
     * @brief Resolve a loot drop id (e.g. "weapon_002") to a WeaponDef.
     *
     * Droptables reference weapon PREFAB ids (weapon_NNN) while weapons.json is
     * keyed by the Gun* class id; the exact weapon_NNN -> Gun* map is the
     * data-pipeline gap (report #3). Best-effort: exact match first, else the
     * trailing integer indexed (mod) into the loaded weapons, so a drop always
     * yields a real, deterministic weapon.
     * @return a WeaponDef, or nullptr if no weapons are loaded.
     */
    const WeaponDef *ResolveDropWeapon(const std::string &dropId) const;

    const BulletDef *FindBullet(const std::string &id) const;
    const EnemyDef *FindEnemy(const std::string &id) const;
    const EnemyGunDef *FindEnemyGun(const std::string &id) const;
    const BuffDef *FindBuff(const std::string &id) const;
    const CharacterDef &PlayerTemplate() const { return m_PlayerTemplate; }

    const std::vector<WeaponDef> &Weapons() const { return m_Weapons; }
    const std::vector<BulletDef> &Bullets() const { return m_Bullets; }
    const std::vector<EnemyDef> &Enemies() const { return m_Enemies; }
    const std::vector<EnemyGunDef> &EnemyGuns() const { return m_EnemyGuns; }
    const std::vector<BuffDef> &Buffs() const { return m_Buffs; }
    /// Floor-1 design-room sizes (the Phase-2 size pool). Empty if the table is
    /// absent -- callers fall back to a fixed room size.
    const std::vector<RoomLayoutDef> &RoomLayouts() const { return m_RoomLayouts; }

private:
    std::vector<WeaponDef> m_Weapons;
    std::vector<BulletDef> m_Bullets;
    std::vector<EnemyDef> m_Enemies;
    std::vector<EnemyGunDef> m_EnemyGuns;
    std::vector<BuffDef> m_Buffs;
    std::vector<RoomLayoutDef> m_RoomLayouts;
    CharacterDef m_PlayerTemplate;

    std::unordered_map<std::string, std::size_t> m_WeaponIndex;
    std::unordered_map<std::string, std::size_t> m_BulletIndex;
    std::unordered_map<std::string, std::size_t> m_EnemyIndex;
    std::unordered_map<std::string, std::size_t> m_EnemyGunIndex;
    std::unordered_map<std::string, std::size_t> m_BuffIndex;
};

} // namespace Game

#endif /* GAME_DATA_HPP */
