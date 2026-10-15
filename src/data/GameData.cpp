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

/// `[layer, order]` array under @p key (P2.17 sorting key), @p fallback when absent/malformed.
SortKey GetSort(const json &j, const char *key, SortKey fallback) {
    auto it = j.find(key);
    if (it != j.end() && it->is_array() && it->size() >= 2 && (*it)[0].is_number() &&
        (*it)[1].is_number()) {
        return SortKey{(*it)[0].get<int>(), (*it)[1].get<int>()};
    }
    return fallback;
}

/// `sprite_offset: [x, y]` (units, prefab-root space) -> (x, y); (0, 0) when absent.
void ReadSpriteOffset(const json &j, float &x, float &y) {
    x = 0.0F;
    y = 0.0F;
    auto it = j.find("sprite_offset");
    if (it != j.end() && it->is_array() && it->size() >= 2) {
        x = (*it)[0].is_number() ? (*it)[0].get<float>() : 0.0F;
        y = (*it)[1].is_number() ? (*it)[1].get<float>() : 0.0F;
    }
}

// A prefab collider object ({shape, radius|size, offset, trigger}, units) under @p key, or a
// Shape::None def when absent/unknown (callers then keep their documented fallback).
ColliderDef ColliderFromJson(const json &node);

ColliderDef GetCollider(const json &j, const char *key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_object()) {
        return ColliderDef{};
    }
    return ColliderFromJson(*it);
}

// P2.18: a list of prefab colliders (`blockers`: the solid Box/Circle colliders that stop a body,
// in prefab-root units) under @p key; empty when absent. Malformed entries are skipped (the
// data gate rejects them before they can ship).
std::vector<ColliderDef> GetColliderList(const json &j, const char *key) {
    std::vector<ColliderDef> out;
    auto it = j.find(key);
    if (it == j.end() || !it->is_array()) {
        return out;
    }
    for (const json &e : *it) {
        if (!e.is_object()) {
            continue;
        }
        const ColliderDef c = ColliderFromJson(e);
        if (c.Present()) {
            out.push_back(c);
        }
    }
    return out;
}

ColliderDef ColliderFromJson(const json &node) {
    ColliderDef c;
    const std::string shape = GetS(node, "shape");
    if (shape == "circle") {
        c.shape = ColliderDef::Shape::Circle;
        c.radius = GetF(node, "radius");
    } else if (shape == "box") {
        c.shape = ColliderDef::Shape::Box;
        auto sz = node.find("size");
        if (sz != node.end() && sz->is_array() && sz->size() >= 2) {
            c.sizeX = (*sz)[0].is_number() ? (*sz)[0].get<float>() : 0.0F;
            c.sizeY = (*sz)[1].is_number() ? (*sz)[1].get<float>() : 0.0F;
        }
    } else {
        return c;
    }
    auto off = node.find("offset");
    if (off != node.end() && off->is_array() && off->size() >= 2) {
        c.offsetX = (*off)[0].is_number() ? (*off)[0].get<float>() : 0.0F;
        c.offsetY = (*off)[1].is_number() ? (*off)[1].get<float>() : 0.0F;
    }
    auto trig = node.find("trigger");
    c.trigger = trig != node.end() && trig->is_boolean() && trig->get<bool>();
    return c;
}

// P3.2: bullets.json "buff.kind" ("fire"/"poison"/...) -> the buffs.json row id
// ("BuffFire"/"BuffPoison"/...) GameData::FindBuff resolves. See
// tools/build_gamedata.py's attach_bullet_buffs()/_BUFF_ID_TO_KIND (inverse map).
std::string BuffIdFromKind(const std::string &kind) {
    if (kind == "fire") return "BuffFire";
    if (kind == "poison") return "BuffPoison";
    if (kind == "ice") return "BuffIce";
    if (kind == "electric") return "BuffElectric";
    if (kind == "armor") return "BuffArmor";
    if (kind == "changespeed") return "BuffChangeSpeed";
    return "";
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
            d.weaponClass = GetS(e, "weapon_class"); // WEAPON RE-KEY
            d.icon = GetS(e, "icon");
            d.bulletSprite = GetS(e, "bullet_sprite");
            d.weaponType = GetI(e, "weapon_type");
            d.atk = GetI(e, "atk");
            d.repel = GetF(e, "repel");
            d.bulletSpeed = GetF(e, "bullet_speed");
            d.bulletPrefab = GetS(e, "bullet_prefab"); // P3.2
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
            d.collider = GetCollider(e, "collider"); // prefab hit collider (units)
            auto buffIt = e.find("buff"); // P3.2
            if (buffIt != e.end() && buffIt->is_object()) {
                d.buffId = BuffIdFromKind(GetS(*buffIt, "kind"));
                d.buffProbability = GetF(*buffIt, "probability");
            }
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
            d.enemyClass = GetS(e, "enemy_class");
            d.biome = GetS(e, "biome");   // CHAPTER PROGRESSION
            d.chapter = GetI(e, "chapter");
            d.aiLevel = GetI(e, "ai_level");
            d.shootCd = GetF(e, "shoot_cd");
            d.friction = GetF(e, "friction");
            d.scoutRate = GetF(e, "scout_rate");
            d.consume = GetI(e, "consume");
            d.eSize = GetI(e, "e_size");
            d.rewardRate = GetI(e, "reward_rate");
            if (e.contains("reward_value") && e["reward_value"].is_array()) {
                const auto &rv = e["reward_value"];
                for (std::size_t k = 0; k < 4 && k < rv.size(); ++k) {
                    d.rewardValue[k] = rv[k].is_number() ? rv[k].get<int>() : 0;
                }
            }
            d.buffImmune = GetF(e, "buff_immune");
            d.kinematic = GetI(e, "kinematic");
            // FAITHFUL @ enemies.csv -- real per-species combat stats.
            d.maxHp = GetI(e, "max_hp");
            d.hp = GetI(e, "hp");
            d.atk = GetI(e, "atk");
            d.speed = GetF(e, "speed");
            d.speedRate = GetF(e, "speed_rate");
            d.camp = GetI(e, "camp");
            d.critical = GetI(e, "critical");
            d.deviation = GetI(e, "deviation");
            d.body = GetCollider(e, "collider");   // physical body circle (units)
            d.hurtbox = GetCollider(e, "hurtbox"); // bullet-hit trigger (units)
            d.bodyScale = GetF(e, "body_scale", 1.0F);
            ReadSpriteOffset(e, d.spriteOffsetX, d.spriteOffsetY);
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
            d.changeValue = GetF(e, "changeValue");
            auto inf = e.find("_inferred");
            if (inf != e.end() && inf->is_object()) {
                d.tickSec = GetF(*inf, "tick_sec");
                d.tickDamage = GetI(*inf, "tick_damage");
                d.speedMul = GetF(*inf, "speed_mul", 1.0F);
                d.armorDelta = GetI(*inf, "armor_delta");
            }
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

    // PER-CHARACTER RE-KEY: load the roster of real per-character stat blocks
    // (FAITHFUL @ c##.prefab RoleAttributePlayer). Old builds stored `roster` as a
    // bare id list; entries are objects now. The default template is then
    // re-pointed at c01 (Knight) so PlayerTemplate() is the faithful default.
    m_Characters.clear();
    m_CharacterIndex.clear();
    if (auto rit = characters.find("roster");
        rit != characters.end() && rit->is_array()) {
        for (const json &c : *rit) {
            if (!c.is_object()) {
                continue; // legacy bare-id-string entry -> no per-char stats
            }
            CharacterDef d;
            d.id = c.value("id", std::string());
            d.name = c.value("name", std::string()); // B5 hero select display name
            d.camp = GetI(c, "camp");
            d.speed = GetF(c, "speed");
            d.speedRate = GetF(c, "speed_rate");
            d.maxHp = GetI(c, "max_hp");
            d.hp = GetI(c, "hp");
            d.maxArmor = GetI(c, "max_armor");
            d.armor = GetI(c, "armor");
            d.maxEnergy = GetI(c, "max_energy");
            d.energy = GetI(c, "_energy");
            d.atk = GetI(c, "atk");
            d.critical = GetI(c, "critical");
            d.skillCd = GetF(c, "skill_cd");
            d.inSkillTime = GetF(c, "in_skill_time");
            d.fireRate = GetF(c, "fire_rate");
            d.body = GetCollider(c, "collider");
            d.hurtbox = GetCollider(c, "hurtbox");
            d.bodyScale = GetF(c, "body_scale", 1.0F);
            ReadSpriteOffset(c, d.spriteOffsetX, d.spriteOffsetY);
            // P3.3.c: hero-specific serialized skill refs + effect-prefab numbers.
            if (auto sr = c.find("skill_refs"); sr != c.end() && sr->is_object()) {
                for (auto it = sr->begin(); it != sr->end(); ++it) {
                    if (!it.value().is_array()) {
                        continue;
                    }
                    std::vector<std::string> stems;
                    for (const json &v : it.value()) {
                        if (v.is_string()) {
                            stems.push_back(v.get<std::string>());
                        }
                    }
                    d.skillRefs[it.key()] = std::move(stems);
                }
            }
            if (auto sp = c.find("skill_params"); sp != c.end() && sp->is_object()) {
                for (auto it = sp->begin(); it != sp->end(); ++it) {
                    if (it.value().is_number()) {
                        d.skillParams[it.key()] = it.value().get<float>();
                    }
                }
            }
            if (!d.id.empty()) {
                m_CharacterIndex[d.id] = m_Characters.size();
                m_Characters.push_back(d);
            }
        }
    }
    if (const CharacterDef *knight = FindCharacter("c01")) {
        m_PlayerTemplate = *knight; // faithful default (was a hand-patched mix)
    }
    // COLLIDER DATA: log once per entity whose prefab carried no physical-body / hurtbox
    // collider. PORT CHOICE (T-52), mirroring MakeBodyGeom (include/sim/BodyGeom.hpp): a
    // missing body uses the hurtbox's inscribed circle and root-space offset; a missing
    // hurtbox makes bullets hit the body circle; only when both are missing does it fall
    // back to the documented placeholder radius.
    const auto fallbackText = [](bool hasBody, bool hasHurtbox) {
        if (!hasBody && hasHurtbox) {
            return "body -> hurtbox inscribed circle (T-52)";
        }
        return hasBody ? "hurtbox -> body circle (T-52)" : "body/hurtbox -> placeholder radius";
    };
    for (const CharacterDef &c : m_Characters) {
        if (!c.body.Present() || !c.hurtbox.Present()) {
            LOG_WARN("GameData: hero {} missing collider: {}", c.id,
                     fallbackText(c.body.Present(), c.hurtbox.Present()));
        }
    }
    for (const EnemyDef &e : m_Enemies) {
        if (!e.body.Present() || !e.hurtbox.Present()) {
            LOG_WARN("GameData: enemy {} missing collider: {}", e.id,
                     fallbackText(e.body.Present(), e.hurtbox.Present()));
        }
    }

    // SPRITE MANIFEST (OPTIONAL, never flips `ok`): per-entity animation frames
    // from sprites.json -- the FAITHFUL prefab -> Animator -> clip -> frame chain.
    // Absent on a checkout without the AssetRipper source; entities then keep
    // their placeholder art.
    m_Sprites.clear();
    m_SpriteIndex.clear();
    const json &sprites = store.Load(dir + "sprites.json");
    if (sprites.is_object()) {
        for (auto it = sprites.begin(); it != sprites.end(); ++it) {
            const json &e = it.value();
            if (!e.is_object()) {
                continue;
            }
            SpriteSet s;
            s.id = it.key();
            s.defaultState = e.value("default", std::string());
            s.sampleRate = GetI(e, "sample_rate");
            if (auto st = e.find("states"); st != e.end() && st->is_object()) {
                for (auto sit = st->begin(); sit != st->end(); ++sit) {
                    std::vector<std::string> frames;
                    if (sit->is_array()) {
                        for (const json &f : *sit) {
                            if (f.is_string()) {
                                frames.push_back(f.get<std::string>());
                            }
                        }
                    }
                    s.states[sit.key()] = std::move(frames);
                }
            }
            if (auto pv = e.find("pivots"); pv != e.end() && pv->is_object()) {
                for (auto pit = pv->begin(); pit != pv->end(); ++pit) {
                    if (pit->is_array() && pit->size() >= 2 && (*pit)[0].is_number() &&
                        (*pit)[1].is_number()) {
                        s.pivots[pit.key()] = {(*pit)[0].get<float>(), (*pit)[1].get<float>()};
                    }
                }
            }
            m_SpriteIndex[s.id] = m_Sprites.size();
            m_Sprites.push_back(std::move(s));
        }
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

    // MAP M1: levels.json -- the recovered MapManager table (31 levels). Absence
    // leaves the scene on its pre-M1 fixed ice tileset / class roster, never
    // flips `ok`.
    const json &levels = store.Load(dir + "levels.json");
    if (levels.is_array()) {
        const auto strList = [](const json &v, const char *key) {
            std::vector<std::string> out;
            const auto it = v.find(key);
            if (it != v.end() && it->is_array()) {
                for (const json &s : *it) {
                    if (s.is_string()) {
                        out.push_back(s.get<std::string>());
                    }
                }
            }
            return out;
        };
        for (const json &v : levels) {
            LevelDef d;
            d.id = GetS(v, "id");
            d.level = GetS(v, "level");
            d.chapter = GetI(v, "chapter");
            d.stage = GetI(v, "stage");
            const std::string var = GetS(v, "variant");
            d.variant = var.empty() ? 'A' : var[0];
            d.biome = GetS(v, "biome");
            d.chestLevel = GetI(v, "chest_level");
            d.mapLong = GetI(v, "map_long");
            if (const auto bg = v.find("camera_bg"); bg != v.end() && bg->is_array()) {
                for (std::size_t k = 0; k < 3 && k < bg->size(); ++k) {
                    d.cameraBg[k] = (*bg)[k].is_number() ? (*bg)[k].get<float>() : 0.0F;
                }
            }
            d.floors = strList(v, "floors");
            if (const auto ws = v.find("walls"); ws != v.end() && ws->is_array()) {
                for (const json &w : *ws) {
                    d.walls.push_back(LevelWallDef{GetS(w, "prefab"), GetS(w, "top"), GetS(w, "face"),
                                                   GetF(w, "face_dy"),
                                                   GetSort(w, "top_sort", SortKey{4, 0}),
                                                   GetSort(w, "face_sort", SortKey{6, 0})});
                }
            }
            if (const auto os = v.find("obstacles"); os != v.end() && os->is_array()) {
                for (const json &o : *os) {
                    d.obstacles.push_back(LevelObstacleDef{GetS(o, "prefab"), GetS(o, "sprite"),
                                                           GetS(o, "alt"), GetF(o, "alt_dy"),
                                                           GetSort(o, "sort", SortKey{4, 0}),
                                                           GetSort(o, "alt_sort", SortKey{6, 0}),
                                                           GetColliderList(o, "blockers")});
                }
            }
            if (const auto rs = v.find("rooms"); rs != v.end() && rs->is_object()) {
                d.rooms.start = strList(*rs, "start");
                d.rooms.type1 = strList(*rs, "type1");
                d.rooms.type2 = strList(*rs, "type2");
                d.rooms.type3 = strList(*rs, "type3");
            }
            d.enemies = strList(v, "enemies");
            d.exEnemies = strList(v, "ex_enemies");
            m_Levels.push_back(std::move(d));
        }
    } else {
        LOG_INFO("GameData: levels.json absent; floors use the fixed pre-M1 tileset/roster");
    }

    // MAP M3: boss_rooms.json -- the 14 r<C>_boss<N> layouts + the prefab skin table.
    const json &bossRooms = store.Load(dir + "boss_rooms.json");
    if (bossRooms.is_object()) {
        if (const auto rooms = bossRooms.find("rooms"); rooms != bossRooms.end() && rooms->is_object()) {
            for (auto it = rooms->begin(); it != rooms->end(); ++it) {
                BossRoomDef d;
                d.id = it.key();
                d.width = GetI(it.value(), "w", 21);
                d.height = GetI(it.value(), "h", 21);
                if (const auto objs = it.value().find("objects"); objs != it.value().end() && objs->is_array()) {
                    for (const json &o : *objs) {
                        d.objects.push_back(BossRoomObject{GetS(o, "p"), GetI(o, "x"), GetI(o, "y")});
                    }
                }
                m_BossRooms.emplace(it.key(), std::move(d));
            }
        }
        if (const auto pf = bossRooms.find("prefabs"); pf != bossRooms.end() && pf->is_object()) {
            for (auto it = pf->begin(); it != pf->end(); ++it) {
                m_PrefabSkins.emplace(it.key(), PrefabSkin{GetS(it.value(), "sprite"), GetS(it.value(), "alt"),
                                                           GetF(it.value(), "alt_dy"), GetS(it.value(), "kind"),
                                                           GetSort(it.value(), "sort", SortKey{4, 0}),
                                                           GetSort(it.value(), "alt_sort", SortKey{6, 0}),
                                                           GetColliderList(it.value(), "blockers")});
            }
        }
    }

    // P2.17: tile_pivots.json -- room-sprite pivots (rgtexture m_Pivot). Absent -> every
    // room sprite keeps the bottom-centre convention (pre-P2.17 behaviour).
    const json &tilePivots = store.Load(dir + "tile_pivots.json");
    if (tilePivots.is_object()) {
        for (auto it = tilePivots.begin(); it != tilePivots.end(); ++it) {
            if (it.value().is_array() && it.value().size() >= 2 && it.value()[0].is_number() &&
                it.value()[1].is_number()) {
                m_TilePivots.emplace(it.key(), std::array<float, 2>{it.value()[0].get<float>(),
                                                                    it.value()[1].get<float>()});
            }
        }
    }

    // M3b: boss_chests.json
    const json &bossChests = store.Load(dir + "boss_chests.json");
    if (bossChests.is_object()) {
        for (auto it = bossChests.begin(); it != bossChests.end(); ++it) {
            BossChestDef d;
            d.id = it.key();
            d.level = GetI(it.value(), "level", 2);
            if (const auto ws = it.value().find("weapons"); ws != it.value().end() && ws->is_array()) {
                for (const json &w : *ws) {
                    if (w.is_string()) {
                        d.weapons.push_back(w.get<std::string>());
                    }
                }
            }
            m_BossChests.emplace(it.key(), std::move(d));
        }
    }

    // M3c: shop.json
    const json &shop = store.Load(dir + "shop.json");
    if (shop.is_object()) {
        if (const auto pots = shop.find("pots"); pots != shop.end() && pots->is_object()) {
            for (auto it = pots->begin(); it != pots->end(); ++it) {
                PotDef d;
                d.id = it.key();
                d.hp = GetI(it.value(), "hp");
                d.energy = GetI(it.value(), "energy");
                d.value = GetI(it.value(), "value");
                d.name = GetS(it.value(), "name");
                d.sprite = GetS(it.value(), "sprite");
                m_Pots.emplace(it.key(), std::move(d));
            }
        }
        if (const auto ms = shop.find("machines"); ms != shop.end() && ms->is_object()) {
            for (auto it = ms->begin(); it != ms->end(); ++it) {
                MachineDef d;
                d.id = it.key();
                d.price = GetI(it.value(), "price");
                d.sprite = GetS(it.value(), "sprite");
                d.display = GetS(it.value(), "display");
                d.blockers = GetColliderList(it.value(), "blockers");
                if (const auto items = it.value().find("items"); items != it.value().end() && items->is_array()) {
                    for (const json &i : *items) {
                        if (i.is_string()) {
                            d.items.push_back(i.get<std::string>());
                        }
                    }
                }
                m_Machines.emplace(it.key(), std::move(d));
            }
        }
        // M3e: shop.json "statues" (statue_0N.prefab TalkStatue). FAITHFUL.
        if (const auto sts = shop.find("statues"); sts != shop.end() && sts->is_object()) {
            for (auto it = sts->begin(); it != sts->end(); ++it) {
                StatueDef d;
                d.id = it.key();
                d.statueIndex = GetI(it.value(), "statue_index");
                d.cost = GetI(it.value(), "cost");
                d.baseSprite = GetS(it.value(), "base_sprite");
                d.bodySprite = GetS(it.value(), "body_sprite");
                d.lightSprite = GetS(it.value(), "light_sprite");
                d.bodyDy = GetF(it.value(), "body_dy");
                d.lightDy = GetF(it.value(), "light_dy");
                d.baseSort = GetSort(it.value(), "base_sort", SortKey{4, 0});
                d.bodySort = GetSort(it.value(), "body_sort", SortKey{6, 0});
                d.lightSort = GetSort(it.value(), "light_sort", SortKey{5, 0});
                d.blockers = GetColliderList(it.value(), "blockers");
                m_Statues.push_back(std::move(d));
            }
        }
    }

    // P1.12: pickups.json
    const json &pickups = store.Load(dir + "pickups.json");
    if (pickups.is_object()) {
        for (auto it = pickups.begin(); it != pickups.end(); ++it) {
            if (it.key() == "_rgcoin") {
                m_CoinFriction = GetF(it.value(), "friction", 0.9F);
                continue;
            }
            if (it.key().rfind("_", 0) == 0) {
                continue;
            }
            PickupDef d;
            d.kind = it.key();
            d.value = GetI(it.value(), "value");
            d.speed = GetF(it.value(), "speed");
            d.range = GetF(it.value(), "range");
            d.frameMs = GetI(it.value(), "frame_ms", 125);
            if (const auto fr = it.value().find("frames"); fr != it.value().end() && fr->is_array()) {
                for (const json &f : *fr) {
                    if (f.is_string()) {
                        d.frames.push_back(f.get<std::string>());
                    }
                }
            }
            m_Pickups.emplace(it.key(), std::move(d));
        }
    } else {
        LOG_INFO("GameData: pickups.json absent (fallback: no drop values -> no drops)");
    }

    // AUDIO: audio.json (absent on a checkout without the sibling _reverse dump
    // / Resources/audio/ -- see tools/build_gamedata.py build_audio_table()).
    // Absence is not a load failure: the game just runs muted (AudioPlayer
    // no-ops on an empty clip name).
    const json &audio = store.Load(dir + "audio.json");
    if (audio.is_object()) {
        if (const auto fx = audio.find("effects"); fx != audio.end() && fx->is_object()) {
            for (auto it = fx->begin(); it != fx->end(); ++it) {
                if (it.value().is_string()) {
                    m_Audio.effects[std::atoi(it.key().c_str())] = it.value().get<std::string>();
                }
            }
        }
        if (const auto bgm = audio.find("bgm"); bgm != audio.end() && bgm->is_object()) {
            m_Audio.bgmMenu = GetS(*bgm, "menu");
            m_Audio.bgmMenuButton = GetS(*bgm, "menu_button");
            m_Audio.bgmRoom = GetS(*bgm, "room");
            if (const auto bb = bgm->find("by_biome"); bb != bgm->end() && bb->is_object()) {
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    AudioTable::Bgm b;
                    b.low = GetS(it.value(), "low");
                    b.high = GetS(it.value(), "high");
                    m_Audio.bgmByBiome.emplace(it.key(), std::move(b));
                }
            }
        }
        if (const auto ws = audio.find("weapon_shoot"); ws != audio.end() && ws->is_object()) {
            for (auto it = ws->begin(); it != ws->end(); ++it) {
                if (it.value().is_string()) {
                    m_Audio.weaponShoot.emplace(it.key(), it.value().get<std::string>());
                }
            }
        }
        if (const auto egs = audio.find("enemy_gun_shoot"); egs != audio.end() && egs->is_object()) {
            for (auto it = egs->begin(); it != egs->end(); ++it) {
                if (it.value().is_string()) {
                    m_Audio.enemyGunShoot.emplace(it.key(), it.value().get<std::string>());
                }
            }
        }
        // AUDIO / T-35: "ui_effects" rows are OBJECTS ({clip, _why, ...}) so each
        // clip carries its provenance in the data; only "clip" is read here.
        if (const auto ui = audio.find("ui_effects"); ui != audio.end() && ui->is_object()) {
            for (auto it = ui->begin(); it != ui->end(); ++it) {
                if (!it.value().is_object()) {
                    continue;
                }
                std::string clip = GetS(it.value(), "clip");
                if (!clip.empty()) {
                    m_Audio.uiEffects.emplace(it.key(), std::move(clip));
                }
            }
        }
    } else {
        LOG_INFO("GameData: audio.json absent (fallback: game runs muted)");
    }

    LOG_INFO("GameData loaded: {} weapons, {} bullets, {} enemies, {} "
             "enemy_guns, {} buffs, {} room layouts, {} design rooms",
             m_Weapons.size(), m_Bullets.size(), m_Enemies.size(),
             m_EnemyGuns.size(), m_Buffs.size(), m_RoomLayouts.size(),
             m_DesignRooms.size());
    return ok;
}

const PickupDef *GameData::FindPickup(const std::string &kind) const {
    auto it = m_Pickups.find(kind);
    return it == m_Pickups.end() ? nullptr : &it->second;
}

const PotDef *GameData::FindPot(const std::string &id) const {
    auto it = m_Pots.find(id);
    return it == m_Pots.end() ? nullptr : &it->second;
}

const MachineDef *GameData::FindMachine(const std::string &id) const {
    auto it = m_Machines.find(id);
    return it == m_Machines.end() ? nullptr : &it->second;
}

const StatueDef *GameData::FindStatue(int statueIndex) const {
    for (const StatueDef &d : m_Statues) {
        if (d.statueIndex == statueIndex) {
            return &d;
        }
    }
    return nullptr;
}

const BossChestDef *GameData::FindBossChest(const std::string &bossPrefab) const {
    // "boss01" -> "chest_boss01" (prefab naming); "bossnian" -> "chest_bossnian".
    const std::string id = bossPrefab.rfind("boss", 0) == 0 ? "chest_" + bossPrefab : bossPrefab;
    auto it = m_BossChests.find(id);
    return it == m_BossChests.end() ? nullptr : &it->second;
}

const BossRoomDef *GameData::FindBossRoom(const std::string &id) const {
    auto it = m_BossRooms.find(id);
    return it == m_BossRooms.end() ? nullptr : &it->second;
}

const PrefabSkin *GameData::FindPrefabSkin(const std::string &prefab) const {
    auto it = m_PrefabSkins.find(prefab);
    return it == m_PrefabSkins.end() ? nullptr : &it->second;
}

std::array<float, 2> GameData::TilePivot(const std::string &stem) const {
    auto it = m_TilePivots.find(stem);
    return it == m_TilePivots.end() ? std::array<float, 2>{0.5F, 0.0F} : it->second;
}

const LevelDef *GameData::FindLevel(const std::string &id) const {
    for (const LevelDef &l : m_Levels) {
        if (l.id == id) {
            return &l;
        }
    }
    return nullptr;
}

const DesignRoomDef *GameData::FindDesignRoom(const std::string &id) const {
    auto it = m_DesignRooms.find(id);
    return it == m_DesignRooms.end() ? nullptr : &it->second;
}

const WeaponDef *GameData::FindWeapon(const std::string &id) const {
    auto it = m_WeaponIndex.find(id);
    if (it != m_WeaponIndex.end()) {
        return &m_Weapons[it->second];
    }
    // WEAPON RE-KEY: callers that still pass a class id ("Gun001", the floor-0
    // default equip, tests) get that family's first prefab in table order.
    return FindWeaponByClass(id);
}
const WeaponDef *GameData::FindWeaponByClass(const std::string &weaponClass) const {
    // Linear scan: 194 prefabs, only on equip/drop -- not per frame. Table order
    // is weapons.csv order, so the pick is deterministic (Gun001 -> weapon_000).
    for (const WeaponDef &w : m_Weapons) {
        if (w.weaponClass == weaponClass) {
            return &w;
        }
    }
    return nullptr;
}
const WeaponDef *GameData::ResolveDropWeapon(const std::string &dropId) const {
    if (const WeaponDef *exact = FindWeapon(dropId)) {
        return exact;
    }
    if (m_Weapons.empty()) {
        return nullptr;
    }
    // WEAPON RE-KEY: weapons.json is keyed by prefab (weapon_NNN), so the exact
    // lookup above now resolves every droptable id. This digit-mod path only
    // covers an id absent from the table (defensive; it was the old stand-in).
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
// PORT CHOICE (T-30): class-keyed row = first 1.7.10 prefab of that class (P1.13 per-instance)
const BulletDef *GameData::FindBullet(const std::string &id) const {
    auto it = m_BulletIndex.find(id);
    return it == m_BulletIndex.end() ? nullptr : &m_Bullets[it->second];
}

const BulletDef *GameData::BulletForWeapon(const WeaponDef &def) const {
    if (def.bulletPrefab.empty()) {
        return nullptr;
    }
    return FindBullet(def.bulletPrefab);
}
const EnemyDef *GameData::FindEnemy(const std::string &id) const {
    auto it = m_EnemyIndex.find(id);
    return it == m_EnemyIndex.end() ? nullptr : &m_Enemies[it->second];
}
const CharacterDef *GameData::FindCharacter(const std::string &id) const {
    auto it = m_CharacterIndex.find(id);
    return it == m_CharacterIndex.end() ? nullptr : &m_Characters[it->second];
}
const SpriteSet *GameData::FindSprites(const std::string &id) const {
    auto it = m_SpriteIndex.find(id);
    return it == m_SpriteIndex.end() ? nullptr : &m_Sprites[it->second];
}
namespace {
// Requested state, else the Animator default (idle), else the usual base
// states, else the lexicographically FIRST state with frames -- deterministic
// on purpose (never unordered_map iteration order; the sim is seeded/golden).
// Shared by SpriteFrames and SpriteFramePivots so the two stay index-aligned.
const std::vector<std::string> *PickSpriteFrames(const SpriteSet &s,
                                                 const std::string &state) {
    auto pick = [&s](const std::string &name) -> const std::vector<std::string> * {
        auto it = s.states.find(name);
        return (it != s.states.end() && !it->second.empty()) ? &it->second : nullptr;
    };
    const std::vector<std::string> *frames = nullptr;
    for (const std::string &name :
         {state, s.defaultState, std::string("ide"), std::string("run")}) {
        if (!name.empty() && (frames = pick(name)) != nullptr) {
            return frames;
        }
    }
    const std::string *best = nullptr;
    for (const auto &kv : s.states) {
        if (!kv.second.empty() && (best == nullptr || kv.first < *best)) {
            best = &kv.first;
        }
    }
    return best != nullptr ? &s.states.at(*best) : nullptr;
}
} // namespace

std::vector<std::string> GameData::SpriteFrames(const std::string &id,
                                                const std::string &resourceRoot,
                                                const std::string &state) const {
    const SpriteSet *s = FindSprites(id);
    if (s == nullptr) {
        return {};
    }
    const std::vector<std::string> *frames = PickSpriteFrames(*s, state);
    if (frames == nullptr) {
        return {};
    }
    std::vector<std::string> out;
    out.reserve(frames->size());
    for (const std::string &f : *frames) {
        out.push_back(resourceRoot + "/sprites/" + f);
    }
    return out;
}
std::vector<std::array<float, 2>> GameData::SpriteFramePivots(const std::string &id,
                                                              const std::string &state) const {
    const SpriteSet *s = FindSprites(id);
    if (s == nullptr) {
        return {};
    }
    const std::vector<std::string> *frames = PickSpriteFrames(*s, state);
    if (frames == nullptr) {
        return {};
    }
    std::vector<std::array<float, 2>> out;
    out.reserve(frames->size());
    for (const std::string &f : *frames) {
        const auto it = s->pivots.find(f);
        out.push_back(it != s->pivots.end() ? it->second : std::array<float, 2>{0.5F, 0.0F});
    }
    return out;
}
int GameData::SpriteIntervalMs(const std::string &id, int fallbackMs) const {
    const SpriteSet *s = FindSprites(id);
    return (s != nullptr && s->sampleRate > 0) ? 1000 / s->sampleRate : fallbackMs;
}

namespace {
const std::string kEmptyClip;
} // namespace

const std::string &GameData::FindEffectClip(int index) const {
    auto it = m_Audio.effects.find(index);
    return it == m_Audio.effects.end() ? kEmptyClip : it->second;
}

const std::string &GameData::WeaponShootClip(const std::string &weaponId) const {
    auto it = m_Audio.weaponShoot.find(weaponId);
    return it == m_Audio.weaponShoot.end() ? kEmptyClip : it->second;
}

const std::string &GameData::EnemyGunShootClip(const std::string &enemyGunId) const {
    auto it = m_Audio.enemyGunShoot.find(enemyGunId);
    return it == m_Audio.enemyGunShoot.end() ? kEmptyClip : it->second;
}

const std::string &GameData::BgmFor(const std::string &biome, bool high) const {
    // P6.7 fix: keyed on the LEVEL's biome (LevelDef::biome), not the chapter.
    // audio.json's bgm.by_biome is generated one row per map_<V><N> prefab's
    // MapManager.bgm_clip, and the six biomes carry five distinct low clips
    // (1-forest bgm_1Low, 2-graveyard bgm_2Low, 3-alien bgm_3Low, 2-castle
    // bgm_4Low, 1-ice bgm_5Low, 3-volcano bgm_6Low) -- a chapter number cannot
    // select between the two variants of its own chapter.
    auto it = m_Audio.bgmByBiome.find(biome);
    if (it == m_Audio.bgmByBiome.end()) {
        return kEmptyClip;
    }
    if (high && !it->second.high.empty()) {
        return it->second.high;
    }
    return it->second.low;
}

const std::string &GameData::UiEffectClip(const std::string &key) const {
    auto it = m_Audio.uiEffects.find(key);
    return it == m_Audio.uiEffects.end() ? kEmptyClip : it->second;
}

const char *ChapterBiome(int chapter) {
    switch (((chapter - 1) % 3 + 3) % 3) {
    case 0:
        return "1-ice";
    case 1:
        return "2-castle";
    default:
        return "3-alien";
    }
}

namespace {
/// e_* base mobs only: no bosses, no ex_ elites, no summoned *_temp parts, no
/// chest mimics (e_chest*) and no static minerals.
bool IsBaseMobId(const std::string &id) {
    if (id.rfind("e_", 0) != 0 || id.rfind("e_chest", 0) == 0) {
        return false;
    }
    return id.size() < 5 || id.compare(id.size() - 5, 5, "_temp") != 0;
}
/// bossNN exactly -- not the boss06_c / boss12_parent / boss12_1 helper parts.
bool IsBossId(const std::string &id) {
    if (id.rfind("boss", 0) != 0 || id.size() == 4) {
        return false;
    }
    for (std::size_t i = 4; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') {
            return false;
        }
    }
    return true;
}
/// Insert keeping the pool sorted by id (pools are tiny; avoids <algorithm>).
void InsertSorted(std::vector<const EnemyDef *> &pool, const EnemyDef *d) {
    auto pos = pool.begin();
    while (pos != pool.end() && (*pos)->id < d->id) {
        ++pos;
    }
    pool.insert(pos, d);
}
} // namespace

std::vector<const EnemyDef *> GameData::ChapterMobPool(int chapter) const {
    const std::string biome = ChapterBiome(chapter);
    std::vector<const EnemyDef *> pool;
    for (const EnemyDef &d : m_Enemies) {
        if (IsBaseMobId(d.id) && (d.chapter == 0 || d.biome == biome)) {
            InsertSorted(pool, &d);
        }
    }
    return pool;
}
// P2.15: boss roster of one biome folder (the floor's REAL biome, LevelDef::biome).
// FAITHFUL @ ExportedProject res/enemy/<biome>/bossNN.prefab (enemies.json biome tag).
// BOSS12 DECISION (PORT CHOICE T-58; the composite reading is INFERRED): boss12_1 and boss12_2 are the two halves of ONE
// composite boss -- identical rows (BossAI12, 500 hp each) and bosses.json has a
// BossAI12Parent (prefab boss12_parent, no enemy_id) as their HP coordinator, while
// the BossAI12 row itself points only at boss12_1. IsBossId() keeps both halves out
// of the pool on purpose: spawning one half as an independent boss would be half a
// boss, and spawning both as separate bosses would be wrong. Until the two-half
// spawn + composite HP exists, 3-volcano therefore rolls only boss11. The original
// spawns BossAI12Parent with the two halves; the port never spawns boss12 (gap).
std::vector<const EnemyDef *> GameData::BiomeBossPool(const std::string &biome) const {
    std::vector<const EnemyDef *> pool;
    for (const EnemyDef &d : m_Enemies) {
        if (IsBossId(d.id) && d.biome == biome) {
            InsertSorted(pool, &d);
        }
    }
    return pool;
}
std::vector<const EnemyDef *> GameData::ChapterBossPool(int chapter) const {
    return BiomeBossPool(ChapterBiome(chapter));
}
const EnemyDef *GameData::FindEnemyByClass(const std::string &enemyClass) const {
    // Linear scan: ~145 species, called only at spawn-roster setup time (not
    // per-frame), so a dedicated class index is not worth the bookkeeping.
    for (const EnemyDef &d : m_Enemies) {
        if (d.enemyClass == enemyClass) {
            return &d;
        }
    }
    return nullptr;
}
// PORT CHOICE (T-30): class-keyed row = first 1.7.10 prefab of that class (P1.13 per-instance)
const EnemyGunDef *GameData::FindEnemyGun(const std::string &id) const {
    auto it = m_EnemyGunIndex.find(id);
    return it == m_EnemyGunIndex.end() ? nullptr : &m_EnemyGuns[it->second];
}
// PORT CHOICE (T-30): class-keyed row = first 1.7.10 prefab of that class (P1.13 per-instance)
const BuffDef *GameData::FindBuff(const std::string &id) const {
    auto it = m_BuffIndex.find(id);
    return it == m_BuffIndex.end() ? nullptr : &m_Buffs[it->second];
}

// (d) dispatch on BuffDef::id -> Combat::BuffKind. See BuffDef's doc comment for
// which fields are FAITHFUL vs INFERRED per kind.
Combat::BuffParams BuffParamsFrom(const BuffDef &d) {
    Combat::BuffParams p;
    p.durationSec = d.buffTime;
    p.tickSec = d.tickSec;
    p.tickDamage = d.tickDamage;
    p.speedMul = d.speedMul;
    p.armorDelta = d.armorDelta;
    if (d.id == "BuffFire") {
        p.kind = Combat::BuffKind::Fire;
    } else if (d.id == "BuffPoison") {
        p.kind = Combat::BuffKind::Poison;
    } else if (d.id == "BuffIce") {
        p.kind = Combat::BuffKind::Ice;
    } else if (d.id == "BuffElectric") {
        p.kind = Combat::BuffKind::Electric;
    } else if (d.id == "BuffArmor") {
        p.kind = Combat::BuffKind::Armor;
    } else if (d.id == "BuffChangeSpeed") {
        p.kind = Combat::BuffKind::ChangeSpeed;
    } else {
        p.kind = Combat::BuffKind::Fire; // unreached by the six known-id callers.
    }
    return p;
}

} // namespace Game
