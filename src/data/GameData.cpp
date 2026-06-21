#include "data/GameData.hpp"

#include <utility>

#include <nlohmann/json.hpp>

#include "Util/DataStore.hpp"
#include "Util/Logger.hpp"

namespace Game {
namespace {

using nlohmann::json;

// Defensive scalar accessors: wrong/missing/null fields fall back to a default
// instead of throwing, so a partial data table still loads.
int GetI(const json &j, const char *key, int fallback = 0) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<int>() : fallback;
}
float GetF(const json &j, const char *key, float fallback = 0.0F) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<float>() : fallback;
}
std::string GetS(const json &j, const char *key,
                 const std::string &fallback = "") {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>()
                                              : fallback;
}

} // namespace

bool GameData::LoadAll(const std::string &resourceRoot) {
    Util::DataStore store;
    const std::string dir = resourceRoot + "/data/";
    bool ok = true;

    const json &weapons = store.Load(dir + "weapons.json");
    if (weapons.is_array()) {
        for (const auto &e : weapons) {
            WeaponDef d;
            d.id = GetS(e, "id");
            d.weaponType = GetI(e, "weapon_type");
            d.atk = GetI(e, "atk");
            d.repel = GetF(e, "repel");
            d.bulletSpeed = GetF(e, "bullet_speed");
            d.critical = GetI(e, "critical");
            d.deviation = GetI(e, "deviation");
            d.consume = GetI(e, "consume");
            d.canThrough = GetI(e, "can_through");
            d.throughCount = GetI(e, "through_count");
            d.weaponSpeed = GetF(e, "weapon_speed", 1.0F);
            d.atkMoveSpeed = GetF(e, "atk_move_speed");
            d.isMelee = GetI(e, "is_melee");
            d.needLock = GetI(e, "need_lock");
            d.itemLevel = GetI(e, "item_level");
            d.itemValue = GetI(e, "item_value");
            d.count = GetI(e, "count");
            d.angle = GetF(e, "angle");
            d.maxTime = GetF(e, "max_time");  // B1-P3: Charge ratio cap (charge guns).
            d.aCount = GetI(e, "a_count");    // B1-P3: charge burst / arrow count.
            m_WeaponIndex[d.id] = m_Weapons.size();
            m_Weapons.push_back(std::move(d));
        }
    } else {
        LOG_ERROR("GameData: weapons.json is not an array");
        ok = false;
    }

    const json &bullets = store.Load(dir + "bullets.json");
    if (bullets.is_array()) {
        for (const auto &e : bullets) {
            BulletDef d;
            d.id = GetS(e, "id");
            d.speed = GetF(e, "speed");
            d.camp = GetI(e, "camp");
            d.destroyTime = GetF(e, "destroy_time");
            d.rotateAngle = GetI(e, "rotate_angle");
            d.canRebound = GetI(e, "can_rebound");
            d.manualDestroy = GetI(e, "manual_destroy");
            m_BulletIndex[d.id] = m_Bullets.size();
            m_Bullets.push_back(std::move(d));
        }
    } else {
        LOG_ERROR("GameData: bullets.json is not an array");
        ok = false;
    }

    const json &enemies = store.Load(dir + "enemies.json");
    if (enemies.is_array()) {
        for (const auto &e : enemies) {
            EnemyDef d;
            d.id = GetS(e, "id");
            d.aiLevel = GetI(e, "ai_level");
            d.shootCd = GetF(e, "shoot_cd");
            d.friction = GetF(e, "friction");
            d.scoutRate = GetF(e, "scout_rate");
            d.consume = GetI(e, "consume");
            d.eSize = GetI(e, "e_size");
            d.rewardRate = GetI(e, "reward_rate");
            d.buffImmune = GetF(e, "buff_immune");
            d.kinematic = GetI(e, "kinematic");
            m_EnemyIndex[d.id] = m_Enemies.size();
            m_Enemies.push_back(std::move(d));
        }
    } else {
        LOG_ERROR("GameData: enemies.json is not an array");
        ok = false;
    }

    const json &enemyGuns = store.Load(dir + "enemy_guns.json");
    if (enemyGuns.is_array()) {
        for (const auto &e : enemyGuns) {
            EnemyGunDef d;
            d.id = GetS(e, "id");
            d.atk = GetI(e, "atk");
            d.repel = GetF(e, "repel");
            d.bulletSpeed = GetF(e, "bullet_speed");
            d.deviation = GetI(e, "deviation");
            d.canThrough = GetI(e, "can_through");
            d.needLock = GetI(e, "need_lock");
            d.facing = GetI(e, "facing");
            m_EnemyGunIndex[d.id] = m_EnemyGuns.size();
            m_EnemyGuns.push_back(std::move(d));
        }
    } else {
        LOG_ERROR("GameData: enemy_guns.json is not an array");
        ok = false;
    }

    const json &buffs = store.Load(dir + "buffs.json");
    if (buffs.is_array()) {
        for (const auto &e : buffs) {
            BuffDef d;
            d.id = GetS(e, "id");
            d.isEnemy = GetI(e, "is_enemy");
            d.buffTime = GetF(e, "buff_time");
            m_BuffIndex[d.id] = m_Buffs.size();
            m_Buffs.push_back(std::move(d));
        }
    } else {
        LOG_ERROR("GameData: buffs.json is not an array");
        ok = false;
    }

    const json &characters = store.Load(dir + "characters.json");
    auto tmpl = characters.find("player_template");
    if (characters.is_object() && tmpl != characters.end() &&
        tmpl->is_object()) {
        const json &p = *tmpl;
        m_PlayerTemplate.camp = GetI(p, "camp");
        m_PlayerTemplate.speed = GetF(p, "speed");
        m_PlayerTemplate.speedRate = GetF(p, "speed_rate");
        m_PlayerTemplate.maxHp = GetI(p, "max_hp");
        m_PlayerTemplate.hp = GetI(p, "hp");
        m_PlayerTemplate.maxArmor = GetI(p, "max_armor");
        m_PlayerTemplate.armor = GetI(p, "armor");
        m_PlayerTemplate.maxEnergy = GetI(p, "max_energy");
        m_PlayerTemplate.energy = GetI(p, "_energy");
        m_PlayerTemplate.atk = GetI(p, "atk");
        m_PlayerTemplate.critical = GetI(p, "critical");
        m_PlayerTemplate.skillCd = GetF(p, "skill_cd");
        m_PlayerTemplate.inSkillTime = GetF(p, "in_skill_time");
        m_PlayerTemplate.fireRate = GetF(p, "fire_rate");
    } else {
        LOG_ERROR("GameData: characters.json missing player_template");
        ok = false;
    }

    // Floor room sizes (Phase-2 size pool). OPTIONAL: absence is not a load
    // failure -- the caller (GameScene) falls back to a fixed room size -- so this
    // never flips `ok`. Extracted from the RE dump by tools/extract_room_layouts.py.
    const json &roomLayouts = store.Load(dir + "room_layouts.json");
    if (roomLayouts.is_array()) {
        for (const auto &e : roomLayouts) {
            RoomLayoutDef d;
            d.id = GetS(e, "id");
            d.width = GetI(e, "w");
            d.height = GetI(e, "h");
            d.type = GetI(e, "type", 1);
            if (d.width > 0 && d.height > 0) {
                m_RoomLayouts.push_back(std::move(d));
            }
        }
    } else {
        LOG_INFO("GameData: room_layouts.json absent; rooms will use the default size");
    }

    // Phase 3: design-room INTERIORS (obj_index obstacle layouts). OPTIONAL like
    // room_layouts -- absence just leaves design slots shell-only, never flips
    // `ok`. JSON OBJECT keyed by room id. Extracted by extract_design_rooms.py.
    const json &designRooms = store.Load(dir + "design_rooms.json");
    if (designRooms.is_object()) {
        for (auto it = designRooms.begin(); it != designRooms.end(); ++it) {
            const json &v = it.value();
            DesignRoomDef d;
            d.width = GetI(v, "w");
            d.height = GetI(v, "h");
            const auto obs = v.find("obstacles");
            if (obs != v.end() && obs->is_array()) {
                d.obstacles.reserve(obs->size());
                for (const json &o : *obs) {
                    d.obstacles.push_back(
                        DesignObstacle{GetI(o, "i"), GetI(o, "x"), GetI(o, "y")});
                }
            }
            m_DesignRooms.emplace(it.key(), std::move(d));
        }
    } else {
        LOG_INFO("GameData: design_rooms.json absent; design slots stay shell-only");
    }

    LOG_INFO("GameData loaded: {} weapons, {} bullets, {} enemies, {} "
             "enemy_guns, {} buffs, {} room layouts, {} design rooms",
             m_Weapons.size(), m_Bullets.size(), m_Enemies.size(),
             m_EnemyGuns.size(), m_Buffs.size(), m_RoomLayouts.size(),
             m_DesignRooms.size());
    return ok;
}

const DesignRoomDef *GameData::FindDesignRoom(const std::string &id) const {
    auto it = m_DesignRooms.find(id);
    return it == m_DesignRooms.end() ? nullptr : &it->second;
}

const WeaponDef *GameData::FindWeapon(const std::string &id) const {
    auto it = m_WeaponIndex.find(id);
    return it == m_WeaponIndex.end() ? nullptr : &m_Weapons[it->second];
}
const WeaponDef *GameData::ResolveDropWeapon(const std::string &dropId) const {
    if (const WeaponDef *exact = FindWeapon(dropId)) {
        return exact;
    }
    if (m_Weapons.empty()) {
        return nullptr;
    }
    // Parse the trailing run of digits (e.g. "weapon_002" -> 2) and index it
    // (mod) into the loaded weapons. Deterministic stand-in until the
    // weapon_NNN -> Gun* map is produced by the data pipeline (report #3).
    int n = 0;
    std::size_t i = dropId.size();
    while (i > 0 && dropId[i - 1] >= '0' && dropId[i - 1] <= '9') {
        --i;
    }
    for (std::size_t k = i; k < dropId.size(); ++k) {
        n = n * 10 + (dropId[k] - '0');
    }
    const std::size_t idx = static_cast<std::size_t>(n) % m_Weapons.size();
    return &m_Weapons[idx];
}
const BulletDef *GameData::FindBullet(const std::string &id) const {
    auto it = m_BulletIndex.find(id);
    return it == m_BulletIndex.end() ? nullptr : &m_Bullets[it->second];
}
const EnemyDef *GameData::FindEnemy(const std::string &id) const {
    auto it = m_EnemyIndex.find(id);
    return it == m_EnemyIndex.end() ? nullptr : &m_Enemies[it->second];
}
const EnemyGunDef *GameData::FindEnemyGun(const std::string &id) const {
    auto it = m_EnemyGunIndex.find(id);
    return it == m_EnemyGunIndex.end() ? nullptr : &m_EnemyGuns[it->second];
}
const BuffDef *GameData::FindBuff(const std::string &id) const {
    auto it = m_BuffIndex.find(id);
    return it == m_BuffIndex.end() ? nullptr : &m_Buffs[it->second];
}

} // namespace Game
