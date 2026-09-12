#ifndef GAME_DATA_HPP
#define GAME_DATA_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Game {

/// Player weapon definition (from Resources/data/weapons.json).
struct WeaponDef {
    std::string id;            ///< prefab id (weapon_NNN) after the WEAPON RE-KEY
    std::string weaponClass;   ///< gun-brain dispatch key (Gun001..); FAITHFUL @ weapons.csv
    std::string icon;          ///< pickup icon PNG under Resources/sprites; '' = placeholder
    std::string bulletSprite;  ///< this weapon's bullet PNG; '' = default bullet_0
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
    /// Charge-ratio cap (weapons.json max_time): the divisor for charge guns
    /// (Gun007/GunHeroBow/GunMagicBow); 0 if absent. B1-P3 loader hole closed.
    float maxTime = 0.0F;
    /// Charge burst / arrow count (weapons.json a_count): Gun007 maxCount,
    /// GunHeroBow arrow count; 0 if absent. B1-P3 loader hole closed.
    int aCount = 0;
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
///
/// ENEMY RE-KEY: as of the per-species pipeline (tools/build_gamedata.py,
/// enemies.csv ground truth), @c id is the PREFAB name (e.g. "boss01",
/// "e_succulent") -- one real species per entry, 145 total -- and @c enemyClass
/// is the shared AI-class dispatch key (e.g. "BossAI01", "EnemyAI07") those 16
/// brain templates are keyed by. GameData::FindEnemy looks up by @c id (species);
/// GameData::FindEnemyByClass looks up a representative species by @c enemyClass
/// for callers that only know the class (e.g. a class-keyed spawner roster).
struct EnemyDef {
    std::string id;
    std::string enemyClass; ///< FAITHFUL: enemy_class @ enemies.csv (AI dispatch key).
    std::string biome;      ///< AssetRipper biome folder ("1-ice", ...); "" = chapter-agnostic
    int chapter = 0;        ///< 1..3 from the biome folder's leading digit; 0 = any
    int aiLevel = 0;
    float shootCd = 0.0F;    ///< seconds between shots
    float friction = 0.0F;
    float scoutRate = 0.0F;
    int consume = 0;         ///< spawn budget cost
    int eSize = 0;
    int rewardRate = 0;         ///< FAITHFUL: reward_rate @ enemies.csv (% drop chance).
    /// FAITHFUL: reward_value @ enemies.csv (Unity int[4]) -- drop COUNTS per kind:
    /// [gold coin_0, silver coin_1, copper coin_2, energy] (see game/EnemyReward.hpp).
    int rewardValue[4] = {0, 0, 0, 0};
    float buffImmune = 0.0F;
    int kinematic = 0;       ///< 1 = turret-style: immovable, ignores knockback
    // --- Real per-species combat stats (FAITHFUL @ enemies.csv; new) ----------
    int maxHp = 0;        ///< FAITHFUL: ra_max_hp @ enemies.csv.
    int hp = 0;           ///< FAITHFUL: ra_hp @ enemies.csv (starting hp; usually == maxHp).
    int atk = 0;          ///< FAITHFUL: damage @ enemies.csv (often 0: the real hit comes
                          ///< from the enemy's bullet/EnemyGunDef, not a flat body atk).
    float speed = 0.0F;   ///< FAITHFUL: ra_speed @ enemies.csv.
    float speedRate = 0.0F; ///< FAITHFUL: ra_speed_rate @ enemies.csv.
    int camp = 0;         ///< FAITHFUL: ra_camp @ enemies.csv (0 = player, 1 = enemy).
    int critical = 0;     ///< FAITHFUL: ra_critical @ enemies.csv.
    int deviation = 0;    ///< FAITHFUL: ra_deviation @ enemies.csv.
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
    std::string id;          ///< charId ("c01".."c13"); empty for the shared template.
    std::string name;        ///< display name (FAITHFUL @ i2 localization, English); "" if unknown
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

/// Per-entity sprite animation set (from Resources/data/sprites.json): the
/// FAITHFUL prefab -> Animator -> clip -> frame chain resolved from the
/// AssetRipper project. Frames are bare PNG names under Resources/sprites/.
struct SpriteSet {
    std::string id;
    std::string defaultState;  ///< the Animator's default (idle) state
    int sampleRate = 0;        ///< clip fps; frame interval = 1000 / sampleRate
    std::unordered_map<std::string, std::vector<std::string>> states;
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

/// One prefab obstacle marker inside a design room, in ROOM-LOCAL grid coords
/// (0..w-1, 0..h-1). @c objIndex is the prefab RGObjectSkin obj_index (Phase-3
/// Step-0 legend: 0 wall, 1-4 box(destructible), 5 trap, 6/7 speed pad,
/// 8 skin_obj(ambiguous), 11 brazier). Collision class is decided at wire time.
struct DesignObstacle {
    int objIndex = 0;
    int x = 0;
    int y = 0;
};

/// One floor-1 design room's static INTERIOR (Phase 3, from design_rooms.json,
/// extracted by tools/extract_design_rooms.py). The shell (perimeter/floor/door)
/// is still RoomGen's; these obstacles replace the procedural obstacle layer.
struct DesignRoomDef {
    int width = 0;
    int height = 0;
    std::vector<DesignObstacle> obstacles;
};

/// MAP M1: one wall prefab of MapManager.wall_list -- its top tile + front face.
struct LevelWallDef {
    std::string prefab;
    std::string top;  ///< sprite stem, e.g. "w601" (Resources/sprites/tiles/<top>.png)
    std::string face; ///< the wall's front face sprite, e.g. "w602"
    float faceDy = 0.0F; ///< w2 child local y (units) -- 0 for walls
};

/// MAP M1: one MapManager.obstacle_list[obj_index] entry (RGObjectSkin skin).
struct LevelObstacleDef {
    std::string prefab; ///< e.g. "box04", "brazier01"
    std::string sprite; ///< main sprite stem (tiles/<sprite>.png)
    std::string alt;    ///< second sprite (broken box / lit face), "" if none
    float altDy = 0.0F; ///< w2 child local y (units): 0.5 = a crate's top sits half a unit up
};

/**
 * @struct LevelDef
 * @brief One floor of the original run, FAITHFUL @ res/map_manager/map_<V><N>.prefab
 *        (the MapManager component): level label, chest tier, walk length, the
 *        biome tileset (6 floor variants + 2 walls), the 12-slot obstacle skin
 *        table, the room prefab pools by type, the WEIGHTED enemy spawn pool
 *        (enemy_list; duplicates are the weights) and the elite pool. Variants
 *        A/B = chapter 1 (forest / ice), C/D = chapter 2 (castle / graveyard),
 *        E/F = chapter 3 (alien / volcano), G1 = the 3-6 end level.
 */
struct LevelDef {
    std::string id;      ///< "B3" = variant B, stage 3
    std::string level;   ///< "1-3"
    int chapter = 0;
    int stage = 0;       ///< 1..5 within the chapter (6 for G1)
    char variant = 'A';
    std::string biome;   ///< "1-ice", ... (matches EnemyDef::biome)
    int chestLevel = 0;  ///< MapManager.chest_level -> RGChest level
    int mapLong = 0;     ///< MapManager.map_long
    float cameraBg[3] = {0.0F, 0.0F, 0.0F};
    std::vector<std::string> floors;          ///< 6 floor sprite stems (floor_list)
    std::vector<LevelWallDef> walls;          ///< wall_list
    std::vector<LevelObstacleDef> obstacles;  ///< obstacle_list[12]
    struct Rooms {
        std::vector<std::string> start, type1, type2, type3;
    } rooms;
    std::vector<std::string> enemies;   ///< enemy_list (weighted spawn pool)
    std::vector<std::string> exEnemies; ///< ex_enemy_list (elites)
};

/// MAP M3: one object of a boss-room prefab (rgprefab/rooms/r<C>_boss<N>): a
/// direct prefab instance placed in the room's wall_group, in room-local cells.
struct BossRoomObject {
    std::string prefab; ///< "box02", "wall_tree1", "brazier03", ...
    int x = 0;
    int y = 0;
};

/// MAP M3: a boss room layout, FAITHFUL @ rgprefab/rooms/r<C>_boss<N>.prefab.
struct BossRoomDef {
    std::string id; ///< "r1_boss1"
    int width = 21;
    int height = 21;
    std::vector<BossRoomObject> objects;
};

/// MAP M3: how a room-object prefab is drawn / collides (boss_rooms.json "prefabs").
struct PrefabSkin {
    std::string sprite; ///< main sprite stem (tiles/<sprite>.png), "" if unresolved
    std::string alt;    ///< w2 overlay stem, "" if none
    float altDy = 0.0F; ///< w2 local y in units
    std::string kind;   ///< "wall" (grid solid) / "box" (destructible) / "solid" / "pad"
};

/// M3b: a boss's chest (res/item/chest_boss<NN>.prefab, RGChestBoss): its drop
/// tier and the signature weapon prefab it always contains (CreateBossWeapon).
struct BossChestDef {
    std::string id; ///< "chest_boss01"
    int level = 2;
    std::vector<std::string> weapons; ///< weapons.json ids ("weapon_077")
};

/// M3c: a consumable pot (RGEnergyPot / RGHealthPot / RGBothPot fields).
struct PotDef {
    std::string id;     ///< "energy_pot"
    int hp = 0;         ///< health restored
    int energy = 0;     ///< energy restored
    int value = 0;      ///< item_value (its price when sold on its own)
    std::string name;   ///< i2 term (item_energy_pot)
    std::string sprite; ///< objects_common_N stem
};

/// M3c: a shop machine (ObjectVendingMachine / ItemWaterDispenser prefab).
struct MachineDef {
    std::string id;                 ///< "vending_machine_i"
    int price = 0;                  ///< item_value: coins per use
    std::vector<std::string> items; ///< object_list (pots); empty = weapon sample
    std::string sprite;             ///< body sprite stem
    std::string display;            ///< display frame sprite stem
};

/// M3e statue (P1.12 continuation): rgprefab/room/statue/statue_0N.prefab
/// TalkStatue (item_value, statue_index) + its 3 SpriteRenderers (base pedestal,
/// worshipped-god body, glow light). FAITHFUL: shop.json "statues".
struct StatueDef {
    std::string id;         ///< "statue_01".."statue_07"
    int statueIndex = 0;    ///< TalkStatue.statue_index (1..7)
    int cost = 0;           ///< TalkStatue.item_value
    std::string baseSprite; ///< pedestal sprite stem (objects3_6 for all 7)
    std::string bodySprite; ///< the worshipped god's sprite stem (varies per statue)
    std::string lightSprite; ///< glow sprite stem (objects3_7 for all 7)
    float bodyDy = 0.0F;     ///< body child local y, units (prefab: 1.1)
    float lightDy = 0.0F;    ///< light child local y, units (prefab: 0.69375)
};

/// P1.12: a coin / energy pickup (res/register/coin_N | energy prefab: RGCoin
/// value / speed / range; coin_0.controller spin frames).
struct PickupDef {
    std::string kind;   ///< "gold" | "silver" | "copper" | "energy"
    int value = 0;
    float speed = 0.0F; ///< units/s
    float range = 0.0F; ///< magnet range, units
    int frameMs = 125;
    std::vector<std::string> frames; ///< sprite stems
};

/// CHAPTER PROGRESSION: the biome folder a chapter draws its mobs/bosses from.
/// The original picks one of two biome variants per chapter (selection rule not
/// recovered); the port fixes the variant whose tileset it has wired (1-ice) and
/// the first of each later pair (2-castle, 3-alien). Chapters wrap after 3.
const char *ChapterBiome(int chapter);

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

    /// Exact prefab id (weapon_NNN); a CLASS id (Gun001..) resolves to the first
    /// prefab of that class in table order (the faithful default of the family,
    /// e.g. Gun001 -> weapon_000, the starter).
    const WeaponDef *FindWeapon(const std::string &id) const;
    const WeaponDef *FindWeaponByClass(const std::string &weaponClass) const;

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
    /// Look up a species by its prefab id (e.g. "e_succulent", "boss01").
    const EnemyDef *FindEnemy(const std::string &id) const;

    /// CHAPTER PROGRESSION (FAITHFUL @ ExportedProject res/enemy/<biome>/):
    /// the base e_* species of the chapter's biome + the chapter-agnostic root
    /// mobs (no bosses, ex_ elites, summoned *_temp parts, chest mimics, or
    /// minerals), sorted by id -- deterministic for the seeded spawn pick.
    std::vector<const EnemyDef *> ChapterMobPool(int chapter) const;
    /// The chapter's bossNN prefabs (helper parts like boss06_c / boss12_parent
    /// excluded), sorted by id.
    std::vector<const EnemyDef *> ChapterBossPool(int chapter) const;
    /// Look up a REPRESENTATIVE species for an AI-class (e.g. "EnemyAI07",
    /// "BossAI01"): the first enemies.csv row with that @c enemyClass, in load
    /// order. For callers that only have a class-keyed roster (e.g. the
    /// EnemyMaker-lite spawner) and need a real, data-driven stand-in species
    /// rather than inventing one. Returns nullptr if no species uses that class.
    const EnemyDef *FindEnemyByClass(const std::string &enemyClass) const;
    const EnemyGunDef *FindEnemyGun(const std::string &id) const;
    const BuffDef *FindBuff(const std::string &id) const;
    const CharacterDef &PlayerTemplate() const { return m_PlayerTemplate; }

    /// Per-character roster with real base stats (FAITHFUL @ c##.prefab). Empty
    /// only on a checkout without the prefab source (then PlayerTemplate is used).
    const CharacterDef *FindCharacter(const std::string &id) const;
    const std::vector<CharacterDef> &Characters() const { return m_Characters; }

    /// SPRITE MANIFEST (sprites.json): the FAITHFUL prefab -> Animator -> clip ->
    /// frame chain per entity id. nullptr => entity keeps its placeholder art.
    const SpriteSet *FindSprites(const std::string &id) const;
    /// Full frame paths (root/sprites/<png>) for @p state, else the entity's
    /// default (idle) state, else a deterministic fallback; empty if unknown id.
    std::vector<std::string> SpriteFrames(const std::string &id,
                                          const std::string &resourceRoot,
                                          const std::string &state = "") const;
    /// Frame interval from the clip's sample rate (1000 / fps), or @p fallbackMs.
    int SpriteIntervalMs(const std::string &id, int fallbackMs) const;

    const std::vector<WeaponDef> &Weapons() const { return m_Weapons; }
    const std::vector<BulletDef> &Bullets() const { return m_Bullets; }
    const std::vector<EnemyDef> &Enemies() const { return m_Enemies; }
    const std::vector<EnemyGunDef> &EnemyGuns() const { return m_EnemyGuns; }
    const std::vector<BuffDef> &Buffs() const { return m_Buffs; }
    /// Floor-1 design-room sizes (the Phase-2 size pool). Empty if the table is
    /// absent -- callers fall back to a fixed room size.
    const std::vector<RoomLayoutDef> &RoomLayouts() const { return m_RoomLayouts; }

    /// Phase 3: the floor-1 design-room interior for @p id (e.g. "r1_42"), or
    /// nullptr when no design table / no such room (slot stays shell-only).
    const DesignRoomDef *FindDesignRoom(const std::string &id) const;

    /// MAP M1: the level table (levels.json). Empty when the table is absent.
    const std::vector<LevelDef> &Levels() const { return m_Levels; }
    /// MAP M1: level by id ("B3"); nullptr when unknown.
    const LevelDef *FindLevel(const std::string &id) const;
    /// MAP M3: boss room layout by prefab id ("r1_boss1"); nullptr when unknown.
    const BossRoomDef *FindBossRoom(const std::string &id) const;
    /// MAP M3: draw / collision info of a room-object prefab; nullptr when unknown.
    const PrefabSkin *FindPrefabSkin(const std::string &prefab) const;
    const std::unordered_map<std::string, BossRoomDef> &BossRooms() const { return m_BossRooms; }
    /// P1.12: pickups.json; nullptr when the table or kind is absent.
    const PickupDef *FindPickup(const std::string &kind) const;
    float CoinFriction() const { return m_CoinFriction; }
    /// M3c: shop tables (shop.json).
    const PotDef *FindPot(const std::string &id) const;
    const MachineDef *FindMachine(const std::string &id) const;
    /// M3e: shop.json "statues" by TalkStatue.statue_index (1..7); nullptr when unknown.
    const StatueDef *FindStatue(int statueIndex) const;
    const std::vector<StatueDef> &Statues() const { return m_Statues; }
    /// M3b: the chest a boss prefab ("boss01") leaves behind; nullptr when none.
    const BossChestDef *FindBossChest(const std::string &bossPrefab) const;
    /// All loaded design-room interiors (empty if design_rooms.json is absent).
    const std::unordered_map<std::string, DesignRoomDef> &DesignRooms() const {
        return m_DesignRooms;
    }

private:
    std::vector<WeaponDef> m_Weapons;
    std::vector<BulletDef> m_Bullets;
    std::vector<EnemyDef> m_Enemies;
    std::vector<EnemyGunDef> m_EnemyGuns;
    std::vector<BuffDef> m_Buffs;
    std::vector<RoomLayoutDef> m_RoomLayouts;
    std::unordered_map<std::string, DesignRoomDef> m_DesignRooms;
    std::vector<LevelDef> m_Levels;
    std::unordered_map<std::string, BossRoomDef> m_BossRooms;
    std::unordered_map<std::string, PrefabSkin> m_PrefabSkins;
    std::unordered_map<std::string, BossChestDef> m_BossChests;
    std::unordered_map<std::string, PotDef> m_Pots;
    std::vector<StatueDef> m_Statues;
    std::unordered_map<std::string, PickupDef> m_Pickups;
    float m_CoinFriction = 0.9F;
    std::unordered_map<std::string, MachineDef> m_Machines;
    CharacterDef m_PlayerTemplate;
    std::vector<CharacterDef> m_Characters;
    std::vector<SpriteSet> m_Sprites;

    std::unordered_map<std::string, std::size_t> m_WeaponIndex;
    std::unordered_map<std::string, std::size_t> m_BulletIndex;
    std::unordered_map<std::string, std::size_t> m_EnemyIndex;
    std::unordered_map<std::string, std::size_t> m_EnemyGunIndex;
    std::unordered_map<std::string, std::size_t> m_BuffIndex;
    std::unordered_map<std::string, std::size_t> m_CharacterIndex;
    std::unordered_map<std::string, std::size_t> m_SpriteIndex;
};

} // namespace Game

#endif /* GAME_DATA_HPP */
