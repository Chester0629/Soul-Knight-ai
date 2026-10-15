#include "scenes/GameScene.hpp"
#include "game/TestHooks.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include <glm/glm.hpp>

#include "Core/Context.hpp"

#include "game/FloorClear.hpp"
#include "game/WorldScale.hpp"
#include "game/RenderLayout.hpp"
#include "game/RunController.hpp"
#include "game/EnemyReward.hpp"
#include "game/EnemyMaker.hpp"
#include "entities/CoinPickup.hpp"

#include "world/FloorBlock.hpp"
#include "world/RoomBlockers.hpp"

#include "sim/BodyGeom.hpp"
#include "sim/BodyMove.hpp"
#include "sim/Depenetration.hpp"
#include "sim/SimConfig.hpp"
#include "sim/SkillDash.hpp"

#include "Util/Animation.hpp"
#include "Util/Collider.hpp"
#include "Util/Image.hpp"
#include "Util/Input.hpp"
#include "Util/SFX.hpp"
#include "Util/Keycode.hpp"
#include "Util/Logger.hpp"
#include "Util/Position.hpp"
#include "Util/TransformUtils.hpp"

namespace Game {
namespace {
constexpr float kBulletLifeMs = 1500.0F;
// World size (pixels) of one RoomGen grid cell (shared with the sprite/collider scale rule).
constexpr float kCellPx = Scale::kCellPx;
// Fallback room footprint (cells) when no room_layouts size pool is loaded. Each
// room is centred inside its MAP_SIZE (41-cell) dungeon block, so blocks tile at a
// 41-cell pitch (1312 px at 32 px/cell) and the corridors carved into the inter-
// block margins meet at the shared seam between adjacent blocks (the "A geometry").
// The door band is centred at block 18-22 for ANY room size, so mixed sizes still
// tile + align. See world/FloorBlock.hpp.
constexpr int kRoomCells = 15;
// Phase 3 design-box durability. PORT DEFAULT -- the real value is set by the
// owner-side prefab/spawn path (RGBox HP @0x0C, not recoverable). 1 = one bullet
// breaks it; tracked as explicit debt (docs/LEVEL_GEN_PLAN.md). Demonstrates the
// "blocks before, passable after" contract; tune when the real value is recovered.
constexpr int kBoxHp = 1;
constexpr float kRoomPitch = static_cast<float>(FloorBlock::kBlock) * kCellPx;
// Player must be this close (px) to auto-open a chest / collect a pickup.
constexpr float kChestOpenRange = 40.0F;
constexpr float kPickupRange = 28.0F;
/// MAP M2: transfer_gate.prefab trigger BoxCollider2D 3x3 units -> ~1.5 units half
/// extent = 22 px + the player body; the gate fires when the player's centre is
/// inside that box.
constexpr float kGateTriggerPx = 30.0F;
/// T-19 PORT CHOICE: spawn scatter impulse (units/s) handed to RGCoin.GetForce; the
/// original's magnitude did not survive (GetReward truncated).
constexpr float kScatterImpulseUnits = 6.0F;
/// PORT CHOICE T-14: statue_index -> term name. shop.json "statues" (built from
/// statue_0N.prefab TalkStatue) carries the real statue_index/cost/sprites/
/// offsets; this name table is the port's own guess at the i2 statue_* term
/// order (the prefab string_name fields are empty in the dump), so it stays a
/// hardcoded array keyed by statue_index (index 0 unused).
const char *const kStatueName[8] = {"", "Knight", "Priest", "Assassin", "Rabbit", "Elf", "Thief", "Sorcerer"};
// SK_AUTOWALK steering speed (px/s): drives headless room->corridor->room traversal.
// PORT CHOICE (T-56, test hook, P3.0 audit): not a data value and not converted (9.4 cells/s, a
// little above the knight's 6.5 u/s walk so the headless runs reach the target in time).
constexpr float kAutoWalkSpeed = 300.0F;
// Inset (px) from a room edge before the clear-room door lock may arm. The door-
// seal cells occupy the outermost cell ring, and a room's interior rect edge
// coincides with that ring's outer face for ANY room size (rect_edge == -16*w ==
// door-cell outer face). So a body whose CENTRE is within one cell (kCellPx) + its
// radius (m_PlayerRadiusPx, from data) of an edge OVERLAPS the seal the instant it snaps shut and
// is pinned at the threshold -- the "air wall" (doc'd: door-seal arms at threshold).
// Arming only once the centre is past that band closes the seal BEHIND the player.
// +1px clears the inclusive circle-vs-AABB tangency at the exact boundary.
// The lock-arm inset is kCellPx + the hero body radius + 1 (45.8 px for the knight), computed inline.

glm::vec2 Normalize(glm::vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return len > 0.0F ? v / len : glm::vec2(0.0F, 0.0F);
}

// --- A (presentation): map a sim cue name to a transient effect sprite + an SFX clip ---
// The per-state BODY animation of an actor (its own attack/hurt/death pose) stays asset-
// blocked: the manifest is flat and every atk_clip/clip_dead ref is an unresolved Unity
// pathid, so there are no per-state frame ranges to switch to. Instead we present each cue
// with a standalone effect sprite (which DO exist) plus a real fx_*.wav, which is the
// "owner plays Animator/sfx/effects" half of the SimEvent design.
std::vector<std::string> EffectFrames(const std::string &root, const std::string &base,
                                      int count) {
    std::vector<std::string> frames;
    frames.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        frames.push_back(root + "/sprites/" + base + "_" + std::to_string(i) + ".png");
    }
    return frames;
}

} // namespace

GameScene::GameScene(int floorSeed, int floorIndex,
                     std::optional<PlayerContinuation> carried, RunController *run)
    : m_BulletPool(0,
                   [] {
                       return std::make_shared<Bullet>(std::string(RESOURCE_DIR));
                   }),
      m_FloorSeed(floorSeed), m_FloorIndex(floorIndex),
      m_Carried(std::move(carried)), m_Run(run) {}

void GameScene::OnEnter() {
    const std::string root = RESOURCE_DIR;
    m_Data.LoadAll(root);
    m_Loot.LoadAll(root); // chest droptables
    m_Audio.emplace(root);
    m_BgmHigh = false;

    // Root every deterministic stream this floor owns in the per-floor seed.
    m_Rng.SetRandomSeed(m_FloorSeed);
    m_RewardRng.SetRandomSeed(m_FloorSeed + 7000); // B3: own window, never perturbs combat
    m_Sim.emplace(m_FloorSeed, this); // headless sim driven from Update; `this` is the WorldCollision.

    // PER-CHARACTER: build the player from the selected character's real stats.
    // charId is carried across floors (default "c01"/Knight until HeroRoom, B5).
    // B5: the hero comes from the continuation (floors 1+), else from the run's
    // chosen hero (RunState::charId, set by the picker); "c01" only for a scene
    // built without a RunController (tests).
    const std::string charId =
        m_Carried ? m_Carried->charId
                  : (m_Run != nullptr ? m_Run->State().charId : std::string("c01"));
    m_CharId = charId;
    LOG_INFO("GameScene: hero {} on floor {}", charId, m_FloorIndex);
    const CharacterDef *cdef = m_Data.FindCharacter(charId);
    m_Player = std::make_shared<Player>(
        cdef != nullptr ? *cdef : m_Data.PlayerTemplate(), root,
        m_Data.SpriteFrames(charId, root), m_Data.SpriteIntervalMs(charId, 80));
    {
        // COLLIDER + SCALE FROM DATA: the hero prefab's body circle / hurtbox / sprite anchor.
        const CharacterDef &hero = cdef != nullptr ? *cdef : m_Data.PlayerTemplate();
        const Sim::BodyGeom pg =
            Sim::MakeBodyGeom(hero.body, hero.hurtbox, Sim::kPlayerBodyRadius);
        m_PlayerRadiusPx = pg.bodyRadiusPx;
        m_Sim->SetPlayerGeom(pg);
        m_Player->SetAnchor(MakeSpriteAnchor(m_Data, charId, hero.body, hero.hurtbox,
                                             hero.bodyScale,
                                             {hero.spriteOffsetX, hero.spriteOffsetY},
                                             pg.bodyRadiusPx));
    }

    // --- Procedural floor: a MapManager random-walk lays out the rooms; each is
    // generated by RoomGen with door openings toward its placed neighbours and
    // tiled edge-to-edge (fixed size) so the door gaps line up and the player can
    // walk between rooms. Deterministic from the run seed.
    // MAP M1: this floor's level from the recovered MapManager table. The run's
    // biome variant (A/B per chapter) comes from the run seed (RunState).
    {
        const int runSeed = m_Run != nullptr ? m_Run->State().runSeed : RunState::kDefaultRunSeed;
        const std::string levelId = LevelIdFor(runSeed, m_FloorIndex);
        m_Level = m_Data.FindLevel(levelId);
        if (m_Level != nullptr) {
            LOG_INFO("Level {} ({}) biome {} chest_level {} map_long {} pool {} species",
                     m_Level->id, m_Level->level, m_Level->biome, m_Level->chestLevel,
                     m_Level->mapLong, m_Level->enemies.size());
        } else {
            LOG_INFO("Level {} not in levels.json -> pre-M1 fixed tileset/roster", levelId);
        }
        // AUDIO (P6.7): this FLOOR's BGM loop, keyed on the level's own biome
        // (LevelDef::biome -> audio.json bgm.by_biome, generated from each
        // map_<V><N> prefab's MapManager.bgm_clip). NOT the chapter: each chapter
        // has two biome variants with different clips (A/B, C/D, E/F), so a
        // chapter number can only ever name one of each pair.
        m_Audio->PlayBgm(m_Data.BgmFor(LevelBiome(), m_BgmHigh));
    }
    MapManager::Options mapOpt;
    // MapManager.map_long counts the NORMAL rooms of the walk; the original adds
    // the start room and the special rooms (type1 chest/sell, type2 gate, type3)
    // on top. The port's walk folds specials into one count, so add those slots
    // (PORT CHOICE T-17: +4 = start + gate + chest/sell + badass).
    mapOpt.mapLong = m_Level != nullptr ? m_Level->mapLong + 4 : 7;
    mapOpt.ranRoomProbability = 25;
    // --- Phase 2: real per-slot design-room selection ---
    // Hand MapManager the floor-1 design-room pool; it assigns each slot a design
    // room (id + size) by type (type-1 fallback for special/badass slots) on a
    // SEPARATE RNG stream, so the walk's draw order -- and thus golden / combat
    // determinism -- is untouched. The slot's roomId is the P3 load handle; its
    // width/height drive the room build (interior stays RoomGen-procedural until
    // P3). The door band is centred (block 18-22) for any size, so mixed sizes
    // still tile and the corridors meet at the seam.
    mapOpt.designRooms.reserve(m_Data.RoomLayouts().size());
    for (const RoomLayoutDef &rl : m_Data.RoomLayouts()) {
        mapOpt.designRooms.push_back(
            DesignRoom{rl.id, rl.width, rl.height, rl.type});
    }
    const MapManager floor(m_FloorSeed, mapOpt);
    const RoomCell &startCell = floor.Rooms()[floor.StartIndex()];
    const int startGx = startCell.gridX;
    const int startGy = startCell.gridY;

    // The room farthest (Manhattan) from the start becomes the boss room.
    int bossRoom = floor.StartIndex();
    int bestDist = -1;
    for (std::size_t i = 0; i < floor.Rooms().size(); ++i) {
        int dx = floor.Rooms()[i].gridX - startGx;
        int dy = floor.Rooms()[i].gridY - startGy;
        const int dist = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (dist > bestDist) {
            bestDist = dist;
            bossRoom = static_cast<int>(i);
        }
    }

    // M3b ROOM ROLES. The original places, besides map_long normal rooms, one of
    // each MapManager pool: type0 r_start, type1 r_chest/r_sell (reward), type2
    // r_transgete / r*_boss* (exit), type3 r_mercenary/r_mineral/r_statue (event).
    // The port's walk only rolls 0..1 special slots, so roles are assigned here
    // (PORT CHOICE T-06) by distance from the start: farthest = exit (gate / boss room), next =
    // reward room, next = event room, the rest normal. Slot codes: 1 normal,
    // 2 exit, 3 event, 4 reward.
    std::vector<int> slotType(floor.Rooms().size(), 1);
    {
        std::vector<std::pair<int, int>> byDist; // (-dist, index)
        for (std::size_t i = 0; i < floor.Rooms().size(); ++i) {
            if (static_cast<int>(i) == floor.StartIndex()) {
                continue;
            }
            const int dx = floor.Rooms()[i].gridX - startGx;
            const int dy = floor.Rooms()[i].gridY - startGy;
            byDist.emplace_back(-((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy)), static_cast<int>(i));
        }
        std::sort(byDist.begin(), byDist.end());
        if (!byDist.empty()) {
            slotType[static_cast<std::size_t>(byDist[0].second)] = 2;
        }
        if (byDist.size() > 1) {
            slotType[static_cast<std::size_t>(byDist[1].second)] = 4;
        }
        if (byDist.size() > 2) {
            slotType[static_cast<std::size_t>(byDist[2].second)] = 3;
        }
    }
    // MAP M2: the transfer gate stands in the exit room (boss floors: the boss room,
    // gate appears once the boss dies).
    m_GateRoom = bossRoom;
    m_BossChestPending = false;
    m_GateActive = false;
    m_Gate = nullptr;
    m_GateLight = nullptr;
    {
        std::string roomLog; // slot -> r1_N (size) -- the realised design selection.
        for (const RoomCell &c : floor.Rooms()) {
            roomLog += (c.roomId.empty() ? "?" : c.roomId) + "(" +
                       std::to_string(c.width > 0 ? c.width : kRoomCells) + "x" +
                       std::to_string(c.height > 0 ? c.height : kRoomCells) + ") ";
        }
        LOG_INFO("Floor {} rooms (design-pool={}): {}", m_FloorIndex,
                 mapOpt.designRooms.size(), roomLog);
    }

    // ENEMY RE-KEY: enemies are now indexed by species prefab id, not AI-class, so
    // resolve "EnemyAI01" via a representative species (FindEnemyByClass).
    const EnemyDef *edef = m_Data.FindEnemyByClass("EnemyAI01");
    // Phase 4 (pure visual): real floor-1 ICE tileset. floor/wall come from biome 6
    // (f601/w601, the snow tiles -- the obstacle_list config's biome 7 was brown);
    // obstacles come from the authoritative obstacle_list[obj_index] chain (common
    // atlas, theme-independent). Extracted by tools/extract_tiles.py.
    // MAP M1: the level's REAL biome tileset -- MapManager.floor_list (six floor
    // variants, indexed by RoomGen's pass-3 roll), wall_list[0] top + front face,
    // and obstacle_list[obj_index] skins. Falls back to the pre-M1 single ice
    // set when the level table is absent.
    // P2.17 (d): every ROOM sprite is point-filtered. FAITHFUL: all gameplay atlases are
    // filterMode 0 (Texture2D/*.png.meta); PORT CHOICE (T-60): only the room sprites are
    // switched here -- characters / bullets / effects / HUD keep PTSD's bilinear default. PTSD's default (bilinear + CLAMP_TO_BORDER)
    // blended each tile's edge texels with the transparent-black border, which showed as
    // dark grid lines between adjacent floor tiles. Util::Image(path, false) selects
    // GL_NEAREST in the existing PTSD API (no vendored-source change).
    const auto roomImage = [](const std::string &path) {
        return std::make_shared<Util::Image>(path, /*useAA=*/false);
    };
    std::vector<std::shared_ptr<Util::Image>> floorImgs;
    std::vector<std::string> floorStems; // parallel to floorImgs (pivot lookup)
    if (m_Level != nullptr) {
        for (const std::string &f : m_Level->floors) {
            if (!f.empty()) {
                floorImgs.push_back(roomImage(root + "/sprites/tiles/" + f + ".png"));
                floorStems.push_back(f);
            }
        }
    }
    if (floorImgs.empty()) {
        floorImgs.push_back(roomImage(root + "/sprites/tiles/floor.png"));
        floorStems.push_back("floor");
    }
    // MapManager.wall_list holds the basic wall + a decorated variant (CreateWall rolls
    // Range(0,100) per tile; the threshold did not survive). Draw the basic wall:
    // the prefab whose name ends in "1" (wall601, wall201, wall_bush1 ...), else [0].
    const LevelWallDef *wallDef = nullptr;
    if (m_Level != nullptr && !m_Level->walls.empty()) {
        wallDef = &m_Level->walls[0];
        for (const LevelWallDef &w : m_Level->walls) {
            if (!w.prefab.empty() && w.prefab.back() == '1') {
                wallDef = &w;
                break;
            }
        }
    }
    const std::string wallTop = (wallDef != nullptr && !wallDef->top.empty())
                                    ? "tiles/" + wallDef->top
                                    : std::string("tiles/wall");
    const std::string wallFace = (wallDef != nullptr && !wallDef->face.empty())
                                     ? "tiles/" + wallDef->face
                                     : wallTop;
    auto wallImg = roomImage(root + "/sprites/" + wallTop + ".png");
    auto wallFaceImg = roomImage(root + "/sprites/" + wallFace + ".png");
    const std::string wallTopStem = wallTop.substr(wallTop.rfind('/') + 1); // "tiles/w601" -> "w601"
    const std::string wallFaceStem = wallFace.substr(wallFace.rfind('/') + 1);
    const float wallFaceDy = wallDef != nullptr ? wallDef->faceDy : 0.0F;
    // FAITHFUL @ wall601.prefab: base on layer 4 (Wall), the w2 overlay on layer 6 (WallFont).
    const SortKey wallTopSort = wallDef != nullptr ? wallDef->topSort : SortKey{4, 0};
    const SortKey wallFaceSort = wallDef != nullptr ? wallDef->faceSort : SortKey{6, 2};
    const glm::vec2 floorSz = floorImgs.front()->GetSize();
    // M3d shadows (PORT CHOICE T-16): the original's wall/prop shadow0 is an OPAQUE
    // black 16x24 block tinted by a multiply material the port has no equivalent
    // for, so it is not drawn; shadow2 (the alpha ellipse from the character atlas, pivot (0.5,0))
    // goes under props, boxes and door bars.
    auto shadowImg2 = roomImage(root + "/sprites/shadow2.png");
    auto doorBaseImg = roomImage(root + "/sprites/door02.png");
    auto doorBarImg = roomImage(root + "/sprites/door01.png");
    // Design-obstacle sprites keyed by obj_index (RGObjectSkin -> obstacle_list).
    // MAP M1: from THIS level's obstacle_list; pre-M1 obj_N crops as fallback.
    std::array<std::shared_ptr<Util::Image>, 12> objImg{};
    std::array<std::shared_ptr<Util::Image>, 12> objAltImg{}; // the prefab's w2 overlay (crate top ...)
    std::array<float, 12> objAltDy{};
    std::array<std::string, 12> objStem{};    // pivot lookup keys (tile_pivots.json)
    std::array<std::string, 12> objAltStem{};
    std::array<SortKey, 12> objSort{};        // prefab SpriteRenderer layer/order (default 4/0)
    std::array<SortKey, 12> objAltSort{};
    objAltSort.fill(SortKey{6, 0});
    for (int i : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}) {
        const std::size_t oi = static_cast<std::size_t>(i);
        if (m_Level != nullptr && oi < m_Level->obstacles.size() &&
            !m_Level->obstacles[oi].sprite.empty()) {
            objImg[oi] = roomImage(
                root + "/sprites/tiles/" + m_Level->obstacles[oi].sprite + ".png");
            objStem[oi] = m_Level->obstacles[oi].sprite;
            objSort[oi] = m_Level->obstacles[oi].sort;
            if (!m_Level->obstacles[oi].alt.empty()) {
                objAltImg[oi] = roomImage(
                    root + "/sprites/tiles/" + m_Level->obstacles[oi].alt + ".png");
                objAltStem[oi] = m_Level->obstacles[oi].alt;
                objAltDy[oi] = m_Level->obstacles[oi].altDy;
                objAltSort[oi] = m_Level->obstacles[oi].altSort;
            }
        } else if (i != 9 && i != 10) {
            objImg[oi] = roomImage(
                root + "/sprites/tiles/obj_" + std::to_string(i) + ".png");
            objStem[oi] = "obj_" + std::to_string(i);
        }
    }
    // The start room repositions the player onto a guaranteed floor cell (set
    // in the loop below); the hardcoded (0,0) spawn can land inside a wall.
    glm::vec2 startFloorPos{0.0F, 0.0F};
    m_Waves.assign(floor.Rooms().size(), SpawnWaves{});
    int roomIndex = 0;
    for (const RoomCell &cell : floor.Rooms()) {
        const int slot = slotType[static_cast<std::size_t>(roomIndex)];
        const glm::vec2 origin{
            static_cast<float>(cell.gridX - startGx) * kRoomPitch,
            static_cast<float>(cell.gridY - startGy) * kRoomPitch};

        RoomGen::Options ropt;
        ropt.randomRoom = false; // explicit per-slot size; interior stays procedural
        // Size = the design room MapManager chose for this slot (cell.roomId); fall
        // back to kRoomCells only when no design pool was loaded.
        const bool haveSize = cell.width > 0 && cell.height > 0;
        ropt.roomWidth = haveSize ? cell.width : kRoomCells;
        ropt.roomHeight = haveSize ? cell.height : kRoomCells;
        ropt.wallLevel = slot >= 2 ? 2 : 1; // richer obstacles in special rooms
        ropt.obstacleLevel = 1;
        // MAP M2: special rooms are EMPTY SHELLS at their prefab size -- r_start
        // 15x15, r_transgete / r_mercenary / r_mineral / r_statue 11x11 (the
        // r1_* design layouts belong to normal rooms only; the type-1 fallback
        // had put crates and speed pads into the portal room). Boss rooms keep
        // the normal-room layout until their 21x21 prefabs are extracted (M3).
        const bool bossShell = roomIndex == bossRoom && IsBossFloor(m_FloorIndex);
        const bool startShell = roomIndex == floor.StartIndex();
        const bool specialShell = (slot == 2 || slot == 3 || slot == 4) && !bossShell;
        if (startShell) {
            ropt.roomWidth = 15;
            ropt.roomHeight = 15;
        } else if (specialShell) {
            ropt.roomWidth = 11;
            ropt.roomHeight = 11;
        }
        // M3c: the reward slot draws r_chest OR r_sell (MapManager.type1_list);
        // the pick is the floor seed's parity (PORT CHOICE T-08 -- the pool draw is
        // not recoverable). r_sell is 19x13.
        const bool sellRoom = slot == 4 && ((m_FloorSeed / 3) % 2 == 1);
        if (sellRoom) {
            ropt.roomWidth = 19;
            ropt.roomHeight = 13;
        }
        if (startShell || specialShell) {
            ropt.proceduralObstacles = false; // bare floor, nothing inside
        }
        // MAP M3: the boss room is one of the level's type2_list prefabs
        // (r1_boss1 ...), FAITHFUL @ rgprefab/rooms: 21x21 with its real object
        // layout. Pick is deterministic from the floor seed (MapManager draws the
        // type-2 pool index from its RGRandom; the exact draw is not recoverable).
        const BossRoomDef *bossLayout = nullptr;
        if (bossShell && m_Level != nullptr && !m_Level->rooms.type2.empty()) {
            const int n = static_cast<int>(m_Level->rooms.type2.size());
            const std::string bid = m_Level->rooms.type2[static_cast<std::size_t>(
                ((m_FloorSeed % n) + n) % n)];
            bossLayout = m_Data.FindBossRoom(bid);
            if (bossLayout != nullptr) {
                ropt.roomWidth = bossLayout->width;
                ropt.roomHeight = bossLayout->height;
                ropt.proceduralObstacles = false;
                for (const BossRoomObject &o : bossLayout->objects) {
                    const PrefabSkin *sk = m_Data.FindPrefabSkin(o.prefab);
                    if (sk != nullptr && sk->kind == "wall") {
                        ropt.designSolidCells.emplace_back(o.x, o.y);
                    }
                }
                LOG_INFO("BossRoom: room {} uses {} ({}x{}, {} objects)", roomIndex, bid,
                         bossLayout->width, bossLayout->height, bossLayout->objects.size());
            }
        }
        // Thread the real floor index for fidelity. NOTE: this is INERT here --
        // RoomGen reads floorIndex ONLY inside its randomRoom==true branch
        // (RoomGen.cpp:53-69) and we build with randomRoom=false, so it does not
        // change generation. The live per-floor difference is m_FloorSeed; Phase 2
        // reconciles the %5 size-narrowing under randomRoom=false (RUN_LOOP_PLAN section 4b).
        ropt.floorIndex = m_FloorIndex;
        // --- Phase 3 (3.1): load the design-room interior for this slot. ---
        // Decision #2 = b': keep RoomGen's shell (perimeter/floor/door-cells),
        // replace the procedural obstacle layer with the prefab obj_index layout.
        // obj_index==0 walls go INTO the grid (connectivity barriers + colliders,
        // via designSolidCells); boxes (1-4) + braziers (11) become a collision
        // OVERLAY below (block movement, but absent from the connectivity grid so
        // they never make a door unreachable); traps(5)/pads(6,7) are non-blocking
        // (deferred skin pass); skin_obj(8) is a no-collider placeholder (collision
        // UNVERIFIED -- see docs/LEVEL_GEN_PLAN.md s7). The obstacle coords are
        // already room-LOCAL (extract_design_rooms.py applied the centre-(20,20)
        // transform -- a CAVEAT reconstruction, validated by in-game play).
        const DesignRoomDef *design =
            (cell.roomId.empty() || startShell || specialShell || bossLayout != nullptr)
                ? nullptr
                : m_Data.FindDesignRoom(cell.roomId);
        if (design != nullptr) {
            ropt.proceduralObstacles = false;
            for (const DesignObstacle &o : design->obstacles) {
                if (o.objIndex == 0) { // wall -> grid (barrier + collider)
                    ropt.designSolidCells.emplace_back(o.x, o.y);
                }
            }
        }
        const RoomGen rg(m_FloorSeed + 1 + roomIndex, cell.entrance, ropt);

        m_Rooms.push_back(
            std::make_unique<Room>(Room::FromRoomGen(rg, kCellPx, origin)));

        // Phase 3 (#3a): the per-room lifecycle authority. Constructed with the map
        // slot type (cell.type: 1 normal -> reward on clear, 2/3 special/badass).
        // Starts Uncleared with doors OPEN (faithful to the prefab door_open=1 /
        // Step 0.5 #3): the room only locks (StartRoom) when the player enters it
        // with hostiles, and reopens on ClearRoom. Lifecycle is zero-RNG; the seed
        // is a parity helper that never perturbs determinism.
        m_RoomLife.emplace_back(slot == 4 ? 2 : slot); // reward room: no clear-reward gate
        m_RoomLife.back().SetSeed(m_FloorSeed + 1 + roomIndex);
        m_RoomLife.back().OpenDoor(); // doors open until StartRoom

        // Corridor-connected floor cells (the door-reachable main region). Spawns
        // anchor here so the player can never start trapped in an obstacle pocket
        // -- RoomGen::FloorList can include cells isolated from every exit. Same
        // source FloorConnectivityTest anchors its reachability assertion on.
        // P2.18: the RAW list; `spawnCells` (the blocker-free subset) is derived below, once this
        // room's box / brazier / obstacle colliders exist.
        const auto allFloorCells = FloorBlock::ConnectedFloorCells(rg);

        // Tile every cell so the floor is actually drawn: a floor sprite under
        // walkable cells (floor/aisle/door), a wall sprite on solid cells.
        // Previously only solid cells were tiled, leaving the floor an
        // unrendered void.
        const auto addTile = [&](const std::shared_ptr<Util::Image> &img,
                                 const glm::vec2 &sz, int cx, int cy, float z) {
            auto tile = std::make_shared<Util::GameObject>();
            tile->SetDrawable(img);
            tile->SetZIndex(z);
            tile->m_Transform.translation = Room::CellToWorld(
                cx, cy, rg.Width(), rg.Height(), kCellPx, origin);
            if (sz.x > 0.0F && sz.y > 0.0F) {
                tile->m_Transform.scale =
                    glm::vec2(kCellPx / sz.x, kCellPx / sz.y);
            }
            m_RoomTiles.push_back(tile);
            m_Renderer.AddChild(tile);
        };
        // P2.17 (a)+(b): ONE placement rule for every room sprite. FAITHFUL @ the room
        // prefabs: the prefab root is the cell's bottom-centre (collider (0, 0.5) 1x1 above
        // it) and a child sits at root + (0, dy units); a Unity sprite is positioned by its
        // PIVOT (tile_pivots.json = rgtexture m_Pivot), so the sprite centre is
        // anchor + (0.5 - pivot) * size * scale (RenderLayout::PivotCentre). A 16x24 (0.5, 0) wall
        // is unchanged; a 16x16 (0.5, 0.5) bush/crate now centres on the anchor instead of
        // floating half a unit high. z comes from the prefab's (sorting layer, order) via
        // RenderLayout::RoomSortZ, y-sorted by the cell anchor (shared by an object's parts).
        const auto addObjSprite = [&](const std::shared_ptr<Util::Image> &img,
                                      const std::string &stem, int cx, int cy, SortKey sort,
                                      float dyUnits = 0.0F)
            -> std::shared_ptr<Util::GameObject> {
            auto tile = std::make_shared<Util::GameObject>();
            tile->SetDrawable(img);
            const float scale = Scale::kSpriteScale; // pixelsToUnits 16 -> 2x cell fit
            tile->m_Transform.scale = glm::vec2(scale, scale);
            const RenderLayout::RoomSpritePlacement pl = RenderLayout::PlaceRoomSprite(
                m_Data, stem, img->GetSize(),
                Room::CellToWorld(cx, cy, rg.Width(), rg.Height(), kCellPx, origin), sort, dyUnits);
            tile->m_Transform.translation = pl.translation;
            tile->SetZIndex(pl.z);
            m_RoomTiles.push_back(tile);
            m_Renderer.AddChild(tile);
            return tile;
        };
        // Shadow under a cell object (layer 3 Shadow). Pivot from the data (shadow2 (0.5,0)).
        // @p mainSort is the object's own sorting key: a floor-layer prop (sting01 spikes,
        // speed pads: layer 2 order 1) must keep its shadow UNDER its plate, as before P2.17,
        // so that shadow sits between the floor tiles and the plate (2.05).
        const auto addShadow = [&](const std::shared_ptr<Util::Image> &img, const std::string &stem,
                                   int cx, int cy, SortKey mainSort) {
            auto sh = addObjSprite(img, stem, cx, cy, SortKey{RenderLayout::kLayerShadow, 0});
            if (mainSort.layer < RenderLayout::kLayerWall) {
                sh->SetZIndex(sh->GetZIndex() - 1.0F + 0.05F); // layer 3 -> 2.05
            }
            return sh;
        };
        for (int x = 0; x < rg.Width(); ++x) {
            for (int y = 0; y < rg.Height(); ++y) {
                if (Room::IsSolidCell(rg.At(x, y))) {
                    // FAITHFUL @ wall601.prefab: the wall is a 16x24 sprite with pivot
                    // (0.5, 0) standing on its cell (collider 1x1 at +0.5) plus the w2
                    // face overlay at the same spot; a 16-px cell is 1 unit, so the
                    // wall rises 1.5 units. Draw both bottom-anchored at native aspect,
                    // sorted by y so a lower wall overdraws the one behind it.
                    // ground under the wall (layer 2 Floor)
                    addObjSprite(floorImgs[0], floorStems[0], x, y, SortKey{RenderLayout::kLayerFloor, 0});
                    // A design-room obj_index-0 wall wears the level's obstacle_list[0]
                    // skin (wall603 ice crystals / wall_tree1 ...), not the perimeter wall.
                    const bool designWall = std::find(ropt.designSolidCells.begin(),
                                                      ropt.designSolidCells.end(),
                                                      std::make_pair(x, y)) != ropt.designSolidCells.end();
                    if (designWall && bossLayout != nullptr) {
                        continue; // MAP M3: the boss-room object pass draws this wall's skin
                    }
                    if (designWall && objImg[0] != nullptr) {
                        addObjSprite(objImg[0], objStem[0], x, y, objSort[0]);
                        if (objAltImg[0] != nullptr) {
                            addObjSprite(objAltImg[0], objAltStem[0], x, y, objAltSort[0], objAltDy[0]);
                        }
                        continue;
                    }
                    addObjSprite(wallImg, wallTopStem, x, y, wallTopSort);
                    if (wallFaceImg != wallImg) {
                        addObjSprite(wallFaceImg, wallFaceStem, x, y, wallFaceSort, wallFaceDy);
                    }
                } else {
                    // MAP M1: the floor variant RoomGen rolled for this cell. Floor
                    // sprites are 16 px wide but may be 24 px tall (castle f2xx: a raised
                    // lip), pivot (0.5,0): draw bottom-anchored at native aspect, upper
                    // rows first, so the lip overlaps the row above instead of squashing.
                    const std::size_t v = static_cast<std::size_t>(rg.FloorVariantAt(x, y));
                    const std::size_t fv = v < floorImgs.size() ? v : 0;
                    addObjSprite(floorImgs[fv], floorStems[fv], x, y, SortKey{RenderLayout::kLayerFloor, 0});
                }
            }
        }

        // Phase 4 design-obstacle VISUALS (+ unchanged Phase 3 collision). Renders
        // each obj_index's REAL sprite (RGObjectSkin -> obstacle_list[obj_index],
        // P4-RE) bottom-anchored at native aspect: pivot (0.5,0), pixelsToUnits 16,
        // so a 16x8 box sits low and a 16x24 prop extends up. PURE VISUAL -- the
        // collision is IDENTICAL to Phase 3: only box (1-4) + brazier (11) get a
        // collider; wall (0) is already a grid solid (rendered as the ice grid wall
        // -- minor: not the wall703 skin, see s7); trap(5)/pad(6,7)/obj(8) stay
        // non-blocking, visual-only. No grid / connectivity / RGRoomX change.
        if (design != nullptr) {
            for (const DesignObstacle &o : design->obstacles) {
                if (o.x < 0 || o.x >= rg.Width() || o.y < 0 || o.y >= rg.Height()) {
                    continue;
                }
                // Decorations live on INTERIOR cells only: the prefab's obj layout is
                // stamped over RoomGen's shell, so a marker that lands on the border
                // (-1), a corridor/aisle cell (-2) or the door band (11) is dropped --
                // exactly like the obj_index-0 walls, which are only stamped onto open
                // floor. (Drawing them there put crystals across doors and over walls.)
                const int cellCode = rg.At(o.x, o.y);
                if (cellCode == -1 || cellCode == -2 || cellCode == 11) {
                    continue;
                }
                const std::size_t oi = static_cast<std::size_t>(o.objIndex);
                if (o.objIndex <= 0 || oi >= objImg.size() ||
                    objImg[oi] == nullptr) {
                    continue; // 0 is the grid wall; 9/10 unused -> null
                }
                addShadow(shadowImg2, "shadow2", o.x, o.y, objSort[oi]); // alpha ellipse under the prop
                auto tile = addObjSprite(objImg[oi], objStem[oi], o.x, o.y, objSort[oi]);
                std::shared_ptr<Util::GameObject> top;
                if (objAltImg[oi] != nullptr) {
                    // FAITHFUL @ box04.prefab: the w2 overlay (crate top, 16x16) sits
                    // alt_dy units above the 16x8 base -> the crate reads as a box. It is
                    // on layer 6 (WallFont): drawn OVER a hero standing behind the crate.
                    top = addObjSprite(objAltImg[oi], objAltStem[oi], o.x, o.y, objAltSort[oi],
                                       objAltDy[oi]);
                }

                // P2.18 (RC4): the collider is the PREFAB's own (levels.json obstacles[].blockers =
                // BoxCollider2D / CircleCollider2D size|radius + offset from the cell's bottom-centre
                // root), not a hard-wired 32x32 cell; obj_index 8/9/10 (obj11_* / obj22_* / signs01)
                // now block too. See DesignObstacleCollider for the per-index rule and fallback.
                const bool box = o.objIndex >= 1 && o.objIndex <= 4;
                const glm::vec2 wpos = Room::CellToWorld(
                    o.x, o.y, rg.Width(), rg.Height(), kCellPx, origin);
                const std::optional<Util::Collider> blockCol =
                    DesignObstacleCollider(m_Level, o.objIndex, wpos, kCellPx);
                if (blockCol.has_value()) {
                    ObstacleBox ob;
                    ob.collider = *blockCol;
                    ob.tile = tile;
                    ob.top = top;
                    ob.destructible = box; // boxes (1-4) break; braziers (11) permanent
                    if (box) {
                        ob.box.SetHp(kBoxHp);
                        ob.box.SetSeed(m_FloorSeed + 1 + roomIndex);
                    }
                    m_RoomObstacles.push_back(std::move(ob));
                }
            }
        }

        // --- MAP M3: boss-room objects -- every prefab instance of the layout, drawn
        //     with its real skin (main + w2 overlay), bottom-anchored and y-sorted;
        //     boxes are destructible blockers, "solid" props permanent blockers,
        //     pads walkable, walls already in the grid (designSolidCells). ---
        if (bossLayout != nullptr) {
            for (const BossRoomObject &o : bossLayout->objects) {
                if (o.x < 0 || o.x >= rg.Width() || o.y < 0 || o.y >= rg.Height()) {
                    continue;
                }
                const int cellCode = rg.At(o.x, o.y);
                if (cellCode == -1 || cellCode == -2 || cellCode == 11) {
                    continue; // never on the border / aisle / door band
                }
                const PrefabSkin *sk = m_Data.FindPrefabSkin(o.prefab);
                if (sk == nullptr || sk->sprite.empty()) {
                    continue; // unresolved prefab (obj22_house, rail ...): nothing drawn
                }
                auto skinImg = [&](const std::string &stem) -> std::shared_ptr<Util::Image> {
                    auto it = m_SkinCache.find(stem);
                    if (it != m_SkinCache.end()) {
                        return it->second;
                    }
                    auto img = roomImage(root + "/sprites/tiles/" + stem + ".png");
                    m_SkinCache.emplace(stem, img);
                    return img;
                };
                if (sk->kind != "pad") {
                    addShadow(shadowImg2, "shadow2", o.x, o.y, sk->sort);
                }
                auto tile = addObjSprite(skinImg(sk->sprite), sk->sprite, o.x, o.y, sk->sort);
                std::shared_ptr<Util::GameObject> top;
                if (!sk->alt.empty()) {
                    top = addObjSprite(skinImg(sk->alt), sk->alt, o.x, o.y, sk->altSort, sk->altDy);
                }
                if (sk->kind == "box" || sk->kind == "solid") {
                    const glm::vec2 wpos = Room::CellToWorld(o.x, o.y, rg.Width(), rg.Height(),
                                                             kCellPx, origin);
                    ObstacleBox ob;
                    // P2.18 (RC4): the prefab's own collider (boss_rooms.json prefabs[].blockers:
                    // obj22_10 1.5x1.5, obj11_05 1x1.2, box_weapon 1.7x1 at -0.15 ...). PORT CHOICE (T-61):
                    // a solid prop whose prefab has no solid collider (mineral shells, whose body
                    // collider sits on the Body layer) keeps the old 32x32 cell.
                    const std::optional<Util::Collider> dataCol = FirstBlocker(sk->blockers, wpos, kCellPx);
                    ob.collider = dataCol.has_value() ? *dataCol
                                                      : Util::Collider::MakeAABB(wpos, glm::vec2{kCellPx, kCellPx});
                    ob.tile = tile;
                    ob.top = top;
                    ob.destructible = sk->kind == "box";
                    if (ob.destructible) {
                        ob.box.SetHp(kBoxHp);
                        ob.box.SetSeed(m_FloorSeed + 1 + roomIndex);
                    }
                    m_RoomObstacles.push_back(std::move(ob));
                }
            }
        }

        // --- A geometry: carve the inter-block corridors toward each connected
        // neighbour (RGAisle-math: 5-wide, aligned to the door band, room edge ->
        // block edge). The walkable strip gets floor tiles; each long flank gets a
        // wall tile + a permanent collider (so the corridor is walled on its sides
        // but open at both ends, where it meets the room door and the neighbour's
        // corridor). The strip geometry is FloorBlock::CorridorStrip -- the SAME
        // source the connectivity tests use, so the tested walk space IS this one.
        const auto blockToWorld = [&](int bx, int by) {
            return Room::CellToWorld(bx, by, FloorBlock::kBlock,
                                     FloorBlock::kBlock, kCellPx, origin);
        };
        const auto addBlockTile = [&](const std::shared_ptr<Util::Image> &img,
                                      const glm::vec2 &sz, int bx, int by,
                                      float z) {
            auto tile = std::make_shared<Util::GameObject>();
            tile->SetDrawable(img);
            tile->SetZIndex(z);
            tile->m_Transform.translation = blockToWorld(bx, by);
            if (sz.x > 0.0F && sz.y > 0.0F) {
                tile->m_Transform.scale = glm::vec2(kCellPx / sz.x, kCellPx / sz.y);
            }
            m_RoomTiles.push_back(tile);
            m_Renderer.AddChild(tile);
        };
        // Corridor sprites use the SAME rule as room tiles: pivot (0.5,0), 2x native
        // size, bottom on the cell bottom edge, z sorted by -y -- so corridor floors
        // and flank walls line up with the perimeter walls instead of squashed
        // 32x32 tiles clashing with the 16x24 walls.
        const auto addBlockSprite = [&](const std::shared_ptr<Util::Image> &img,
                                        const std::string &stem, int bx, int by, SortKey sort,
                                        float dyUnits = 0.0F) {
            auto tile = std::make_shared<Util::GameObject>();
            tile->SetDrawable(img);
            const float scale = Scale::kSpriteScale;
            tile->m_Transform.scale = glm::vec2(scale, scale);
            const RenderLayout::RoomSpritePlacement pl = RenderLayout::PlaceRoomSprite(
                m_Data, stem, img->GetSize(), blockToWorld(bx, by), sort, dyUnits);
            tile->m_Transform.translation = pl.translation;
            tile->SetZIndex(pl.z);
            m_RoomTiles.push_back(tile);
            m_Renderer.AddChild(tile);
            return tile;
        };
        const auto addCorridorWall = [&](int bx, int by) {
            addBlockSprite(floorImgs.front(), floorStems.front(), bx, by,
                           SortKey{RenderLayout::kLayerFloor, 0}); // ground under the wall
            addBlockSprite(wallImg, wallTopStem, bx, by, wallTopSort);
            if (wallFaceImg != wallImg) {
                addBlockSprite(wallFaceImg, wallFaceStem, bx, by, wallFaceSort, wallFaceDy);
            }
            m_Corridors.push_back(Util::Collider::MakeAABB(
                blockToWorld(bx, by), glm::vec2{kCellPx, kCellPx}));
        };
        for (int dir = 0; dir < 4; ++dir) {
            if (cell.entrance[static_cast<std::size_t>(dir)] != 1) {
                continue;
            }
            const FloorBlock::Rect s = FloorBlock::CorridorStrip(dir, rg);
            for (int bx = s.x0; bx <= s.x1; ++bx) {
                for (int by = s.y0; by <= s.y1; ++by) {
                    addBlockSprite(floorImgs.front(), floorStems.front(), bx, by,
                                   SortKey{RenderLayout::kLayerFloor, 0}); // walkable corridor floor
                }
            }
            const bool horiz = (dir == FloorBlock::DIR_EAST ||
                                dir == FloorBlock::DIR_WEST);
            if (horiz) { // flank the long (x) sides with walls
                for (int bx = s.x0; bx <= s.x1; ++bx) {
                    addCorridorWall(bx, s.y0 - 1);
                    addCorridorWall(bx, s.y1 + 1);
                }
            } else { // flank the long (y) sides with walls
                for (int by = s.y0; by <= s.y1; ++by) {
                    addCorridorWall(s.x0 - 1, by);
                    addCorridorWall(s.x1 + 1, by);
                }
            }
        }

        // Collect this room's FULL door opening (perimeter aisle band + the two
        // code-11 flanks, via FloorBlock::DoorSealCells) as colliders, sealed while
        // the room is the active uncleared room (clear-room gating). Sealing only
        // the code-11 cells would leave the 5-wide aisle band open and the locked
        // room would contain nobody -- the seal must span the whole opening.
        std::vector<Util::Collider> doors;
        for (const auto &c : FloorBlock::DoorSealCells(rg)) {
            doors.push_back(Util::Collider::MakeAABB(
                Room::CellToWorld(c.first, c.second, rg.Width(), rg.Height(),
                                  kCellPx, origin),
                glm::vec2{kCellPx, kCellPx}));
        }
        m_RoomDoors.push_back(std::move(doors));
        // M3d FAITHFUL doors @ r1_*.prefab aisle_*/door: five pieces (d1..d5) across
        // the door band, each a door02 base with a door01 bar one unit up whose
        // shadow + bar are enabled only while the doors are closed (door_close2 /
        // door_open clips). Base always visible; bar shown while sealed.
        for (const auto &c : FloorBlock::DoorSealCells(rg)) {
            if (rg.At(c.first, c.second) != -2) {
                continue; // d1..d5 sit on the five aisle cells; the code-11 flanks are the frame
            }
            // FAITHFUL @ door_n.prefab: base d1..d5 (door02) layer 4, shadow layer 3 order 1,
            // bar `d` (door01) layer 6 order 3 at local (0, 1).
            addObjSprite(doorBaseImg, "door02", c.first, c.second, SortKey{RenderLayout::kLayerWall, 0});
            DoorPiece piece;
            piece.room = roomIndex;
            // Same PlaceRoomSprite path as every other room sprite; the two pieces start
            // hidden (scale 0) and the door-state loop reveals them with kSpriteScale.
            const glm::vec2 doorCell =
                Room::CellToWorld(c.first, c.second, rg.Width(), rg.Height(), kCellPx, origin);
            const RenderLayout::RoomSpritePlacement shPl = RenderLayout::PlaceRoomSprite(
                m_Data, "shadow2", shadowImg2->GetSize(), doorCell,
                SortKey{RenderLayout::kLayerShadow, 1});
            piece.shadow = std::make_shared<Util::GameObject>();
            piece.shadow->SetDrawable(shadowImg2);
            piece.shadow->m_Transform.scale = {0.0F, 0.0F};
            piece.shadow->m_Transform.translation = shPl.translation;
            piece.shadow->SetZIndex(shPl.z);
            m_Renderer.AddChild(piece.shadow);
            const RenderLayout::RoomSpritePlacement barPl = RenderLayout::PlaceRoomSprite(
                m_Data, "door01", doorBarImg->GetSize(), doorCell,
                SortKey{RenderLayout::kLayerWallFont, 3}, /*dyUnits=*/1.0F); // d at (0, 1)
            piece.bar = std::make_shared<Util::GameObject>();
            piece.bar->SetDrawable(doorBarImg);
            piece.bar->m_Transform.scale = {0.0F, 0.0F}; // hidden: doors start open
            piece.bar->m_Transform.translation = barPl.translation;
            piece.bar->SetZIndex(barPl.z);
            m_Renderer.AddChild(piece.bar);
            m_DoorPieces.push_back(std::move(piece));
        }

        // P2.18 (RC1) PORT CHOICE (T-63): the connected floor list ignores the box
        // / brazier overlay (RoomGen stamps no boxes into the grid), so it contained cells INSIDE
        // boxes -- enemies spawned there stayed pinned for good. Keep only the cells where a hero-
        // sized body is free of every wall, corridor flank, standing obstacle and door-seal
        // collider that exists now (SpawnBlocked: the seal band counts even while the doors are open). Order is preserved (the spread formula index*7+3 % size is
        // unchanged and no RNG is drawn); an all-blocked room falls back to the raw list.
        // A species with a bigger body is re-seated per spawn in SpawnEnemy (ResolveSpawnPoint).
        std::vector<std::pair<int, int>> spawnCells = FilterFreeCells(
            allFloorCells, rg.Width(), rg.Height(), kCellPx, origin, m_PlayerRadiusPx,
            [this, roomIndex](glm::vec2 p, float r) {
                return SpawnBlocked(static_cast<std::size_t>(roomIndex), p, r);
            });
        m_SpawnCellsRemoved += static_cast<int>(allFloorCells.size() - spawnCells.size());
        m_SpawnCellsTotal += static_cast<int>(allFloorCells.size());
        if (spawnCells.empty()) {
            spawnCells = allFloorCells;
        }
        if (roomIndex == m_GateRoom && !spawnCells.empty()) {
            // The gate stands at the room's b_point = its geometric centre cell
            // ((w-1)/2, (h-1)/2); if that cell is not open floor, the nearest
            // connected floor cell to it.
            const int gx = (rg.Width() - 1) / 2;
            const int gy = (rg.Height() - 1) / 2;
            std::pair<int, int> best = spawnCells[spawnCells.size() / 2];
            int bestD = 1 << 30;
            for (const auto &c : spawnCells) {
                const int d = (c.first - gx) * (c.first - gx) + (c.second - gy) * (c.second - gy);
                if (d < bestD) {
                    bestD = d;
                    best = c;
                }
            }
            m_GatePos = Room::CellToWorld(best.first, best.second, rg.Width(), rg.Height(),
                                          kCellPx, origin);
        }

        // Put the player on a corridor-connected floor cell of the start room
        // (spawning at the origin can land in a wall, and a raw FloorList cell can
        // be an isolated pocket -> the player would start unable to reach a door).
        if (roomIndex == floor.StartIndex() && !spawnCells.empty()) {
            const auto &pc = spawnCells[spawnCells.size() / 2];
            startFloorPos = Room::CellToWorld(pc.first, pc.second, rg.Width(),
                                              rg.Height(), kCellPx, origin);
        }

        // Boss room gets a boss; every other non-start room gets one enemy.
        if (roomIndex != floor.StartIndex() && !spawnCells.empty()) {
            const auto &fc = spawnCells[spawnCells.size() / 2];
            glm::vec2 spawn = Room::CellToWorld(
                fc.first, fc.second, rg.Width(), rg.Height(), kCellPx, origin);
            if (roomIndex == bossRoom && IsBossFloor(m_FloorIndex)) {
                // Chapter gate: the boss only spawns on the chapter's LAST floor
                // (IsBossFloor, RunState.hpp); on mob floors (index 0..3) the
                // would-be boss room falls into the else-branch below and spawns
                // one ordinary mob, so the floor stays populated and clearable.
                // BossMaker-lite (B1-P2, policy C): pick the boss from a
                // representative roster of FIRING bosses across distinct attack-
                // dispatch shapes -- AI01 (chase + ungated ChooseAttack), AI08
                // (param-gated ChooseAttack), AI11 (field-gated ShootReflection),
                // AI06 (summon-parent), AI12 (composite-half) -- proving the (d)
                // boss dispatch in-game. The pick is a pure DETERMINISTIC function
                // of (room type, boss-room index): it draws NOTHING from the
                // per-floor RNG, so golden/combat determinism is untouched. The
                // dispatch (BrainFactory -> (d) adapter) routes the id to the
                // matching brain; AI06 summon / AI12 composite-HP are no-op (debt
                // s7). floor->boss is NOT recoverable (no boss_list; the dump is
                // chapter-3) -> reversible C downgrade pending a chapter-1 dump.
                // P2.15 BOSS POOL BY REAL BIOME: pick from the roster of THIS
                // floor's actual biome (LevelDef::biome, chosen per run seed:
                // A forest / B ice, C castle / D graveyard, E alien / F volcano),
                // FAITHFUL @ ExportedProject res/enemy/<biome>/bossNN: 1-forest
                // boss07/08, 1-ice boss09/10/13, 2-castle boss01/02, 2-graveyard
                // boss03/04, 3-alien boss05/06, 3-volcano boss11 (boss12 halves are
                // not poolable, see GameData::BiomeBossPool). Without levels.json the
                // old fixed-per-chapter biome is kept. The pick stays a pure
                // DETERMINISTIC function of (room type, boss-room index) -- nothing
                // is drawn from the per-floor RNG.
                const std::string bossBiome = m_Level != nullptr
                                                   ? LevelBiome()
                                                   : std::string(ChapterBiome(ChapterOf(m_FloorIndex)));
                const std::vector<const EnemyDef *> bossPool = m_Data.BiomeBossPool(bossBiome);
                const EnemyDef *bossDef =
                    bossPool.empty()
                        ? m_Data.FindEnemyByClass("BossAI01") // defensive fallback
                        : bossPool[(static_cast<std::size_t>(slot) +
                                    static_cast<std::size_t>(bossRoom)) %
                                   bossPool.size()];
                // bossId = the AI class (the sim's brain-dispatch key). The PREFAB id
                // keys the real maxHp (ENEMY RE-KEY) and the sprite manifest -- which
                // is prefab-keyed: passing the class here in B2-P3 had left bosses on
                // the boss01 placeholder frames.
                const std::string bossId =
                    bossDef != nullptr ? bossDef->enemyClass : std::string("BossAI01");
                const std::string bossPrefab = bossDef != nullptr ? bossDef->id : bossId;
                m_BossDef = bossDef;
                {
                    // P2.18 (T-63): seat the boss where ITS body (r up to 32 px) is free: the
                    // nearest free cell of this room, else a depenetrated centre.
                    std::vector<glm::vec2> cellPts;
                    for (const auto &sc : spawnCells) {
                        cellPts.push_back(Room::CellToWorld(sc.first, sc.second, rg.Width(),
                                                            rg.Height(), kCellPx, origin));
                    }
                    const float bossR = bossDef != nullptr
                                            ? Sim::MakeBodyGeom(bossDef->body, bossDef->hurtbox,
                                                                Sim::kBossBodyRadius).bodyRadiusPx
                                            : Sim::kBossBodyRadius;
                    const auto blocks = [this, roomIndex](glm::vec2 p, float r) {
                        return SpawnBlocked(static_cast<std::size_t>(roomIndex), p, r);
                    };
                    spawn = ResolveSpawnPoint(cellPts, spawn, bossR, blocks);
                    spawn = Depenetrate(spawn, bossR);
                }
                LOG_INFO("BossPool: biome {} -> {} (pool {})", bossBiome, bossPrefab,
                         bossPool.size());
                const int bossMaxHp = (bossDef != nullptr && bossDef->maxHp > 0)
                                          ? bossDef->maxHp
                                          : 500;
                auto boss = std::make_shared<Boss>(
                    root, spawn, bossMaxHp, /*shootCd=*/2.0F, m_FloorSeed + 9000,
                    m_Data.SpriteFrames(bossPrefab, root),
                    m_Data.SpriteIntervalMs(bossPrefab, 150));
                if (bossDef != nullptr) {
                    // SCALE/PIVOT/COLLIDER FROM DATA (boss body scale is 1 in every boss
                    // prefab; the sprite GameObject offset is not: boss05, boss11, boss12).
                    boss->SetAnchor(MakeSpriteAnchor(
                        m_Data, bossPrefab, bossDef->body, bossDef->hurtbox, bossDef->bodyScale,
                        {bossDef->spriteOffsetX, bossDef->spriteOffsetY}, Sim::kBossBodyRadius));
                }
                boss->SetRoomId(roomIndex);
                m_Bosses.push_back(boss);
                m_Sim->SetBoss(2.0F, spawn, bossMaxHp, roomIndex,
                               m_FloorSeed + 9000, bossId, bossDef);
                LOG_INFO("BossMaker-lite: room {} spawned {} maxHp={} (type {})", roomIndex,
                         bossId, bossMaxHp, slot);
            } else if (slot != 1) {
                // MAP M2: every special room prefab (r_start, r_transgete, r_chest,
                // r_sell, r_statue, r_mercenary, r_mineral, the boss rooms) carries an
                // EnemyMaker with level 0 -> StartEnemyMaker spawns NOTHING. Only the
                // r1_* normal rooms (type 1) fight. (type3_list = mercenary/mineral/
                // statue event rooms, not a badass tier.)
                LOG_INFO("EnemyMaker: room {} (type {}) special prefab -> level 0, no spawns",
                         roomIndex, slot);
            } else {
                // MAP M1: the original EnemyMaker over the level's REAL spawn pool.
                // FAITHFUL @ MapManager.enemy_list (weighted: duplicates are the
                // weights) + EnemyMaker.StartEnemyMaker (level -> enemy_point budget,
                // game_typed.c:77871-77995). INFERRED (T-05): the big-room bump.
                // INFERRED (T-04): how CreatePart/CreateEnemy charge each pick's
                // consume -- CreateEnemy is truncated after rg_random.Range(0, 200). Badass (type 3) rooms add
                // one elite from ex_enemy_list (always_badass). The maker draws from its OWN
                // stream (m_FloorSeed + 1500 + room) so map/combat replay is untouched.
                // Falls back to the chapter mob pool when the level table is absent.
                std::vector<std::string> pool;
                if (m_Level != nullptr) {
                    pool = m_Level->enemies;
                } else {
                    for (const EnemyDef *d : m_Data.ChapterMobPool(ChapterOf(m_FloorIndex))) {
                        pool.push_back(d->id);
                    }
                }
                RGRandom maker;
                maker.SetRandomSeed(m_FloorSeed + 1500 + roomIndex);
                const int stage = m_Level != nullptr ? m_Level->stage : (m_FloorIndex % kChapterFloors) + 1;
                const int level = EnemyMakerLevel(stage, rg.Width(), rg.Height());
                const int points = RollEnemyPoints(level, maker);
                std::vector<std::string> ids = PlanRoomSpawns(
                    pool,
                    [this](const std::string &id) {
                        const EnemyDef *d = m_Data.FindEnemy(id);
                        return d != nullptr ? d->consume : 1;
                    },
                    points, maker);
                // Elites: EnemyMaker.badass_rate promotes a pick to ex_enemy_list. The
                // rate is a private field that did not survive decompilation --
                // PORT CHOICE T-03: stage * 3 % per pick (chapter-scaled), labelled so a
                // recovered value can replace it in one place.
                if (m_Level != nullptr && !m_Level->exEnemies.empty()) {
                    for (std::string &id : ids) {
                        if (maker.Range(0, 100) < stage * 3) {
                            id = m_Level->exEnemies[static_cast<std::size_t>(
                                maker.Range(0, static_cast<int>(m_Level->exEnemies.size())))];
                        }
                    }
                }
                // Spawn points: the room's connected floor cells, spread evenly.
                SpawnWaves &wv = m_Waves[static_cast<std::size_t>(roomIndex)];
                for (const auto &sc : spawnCells) {
                    wv.points.push_back(Room::CellToWorld(sc.first, sc.second, rg.Width(),
                                                          rg.Height(), kCellPx, origin));
                }
                // FAITHFUL: StartEnemyMaker -> max_room_size = level * 3 + max_room_size
                // (game_typed.c:77995; the private field is 0 on rooms): the size of
                // each part. INFERRED (T-04): CreateEnemy's per-pick decrement of
                // room_size/enemy_point is truncated, so a part is a fixed level*3 picks.
                wv.wave.Plan(ids, level * 3);
                SpawnNextWave(roomIndex);
                LOG_INFO("EnemyMaker: room {} (type {}) level {} points {} -> {} planned, wave size {}",
                         roomIndex, slot, level, points, ids.size(), wv.wave.waveSize);
            }
        }

        // M3b FAITHFUL chests: only the REWARD room (type1_list r_chest) holds a
        // chest -- chest_room_reward at its b_point (RGChestRoomReward, drop tier
        // = MapManager.chest_level). Boss chests appear where the boss dies
        // (RGChestBoss). Normal rooms hold none.
        if (slot == 4 && sellRoom && !spawnCells.empty()) {
            // M3c SHOP (r_sell): vending_machine_i (a random pot from its
            // object_list for item_value coins), vending_machine_w (a sample weapon
            // from the level's chest tier for item_value coins) and the free
            // water_dispenser, in a row across the upper half (the prefab has no
            // fixed slots; MapManager places them at run time -- PORT CHOICE T-11 layout).
            RGRandom shopRng;
            shopRng.SetRandomSeed(m_FloorSeed + 3000);
            const int cx = (rg.Width() - 1) / 2;
            const int cy = (rg.Height() - 1) / 2 + 2;
            const auto place = [&](const char *id, int dxCells) {
                const MachineDef *md = m_Data.FindMachine(id);
                if (md == nullptr) {
                    return;
                }
                VendingMachine::Kind kind = VendingMachine::Kind::Water;
                std::string stock;
                std::string stockSprite;
                if (std::string(id) == "vending_machine_i") {
                    kind = VendingMachine::Kind::Item;
                    if (!md->items.empty()) {
                        stock = md->items[static_cast<std::size_t>(
                            shopRng.Range(0, static_cast<int>(md->items.size())))];
                        if (const PotDef *pd = m_Data.FindPot(stock)) {
                            stockSprite = pd->sprite;
                        }
                    }
                } else if (std::string(id) == "vending_machine_w") {
                    kind = VendingMachine::Kind::Weapon;
                    const std::string dropId = m_Loot.Roll(
                        m_Level != nullptr ? m_Level->chestLevel : 1, shopRng);
                    if (const WeaponDef *wd = m_Data.ResolveDropWeapon(dropId)) {
                        stock = wd->id;
                        stockSprite = wd->icon.size() > 4 ? wd->icon.substr(0, wd->icon.size() - 4) : "";
                    }
                }
                const glm::vec2 at = Room::CellToWorld(cx + dxCells, cy, rg.Width(), rg.Height(),
                                                       kCellPx, origin);
                auto m = std::make_shared<VendingMachine>(root, at, kind, md->price, md->sprite,
                                                          md->display, stock, stockSprite);
                m_Machines.push_back(m);
                m_Renderer.AddChild(m);
                if (m->Display()) {
                    m_Renderer.AddChild(m->Display());
                }
                if (m->Icon()) {
                    m_Renderer.AddChild(m->Icon());
                }
                m_Renderer.AddChild(m->PriceTag());
                // P2.18 (RC4): the machine prefab's own `collider` child (vending_machine_i 2x1,
                // vending_machine_w 2.5x1, water_dispenser 1x1; shop.json machines[].blockers).
                // Fallback: the old 2x1-cell box.
                const std::optional<Util::Collider> machCol = FirstBlocker(md->blockers, at, kCellPx);
                m_RoomObstacles.push_back(ObstacleBox{
                    machCol.has_value() ? *machCol
                                        : Util::Collider::MakeAABB(at, glm::vec2{2.0F * kCellPx, kCellPx}),
                    RGBox{}, nullptr, false, true});
                LOG_INFO("Shop: {} at room {} sells '{}' for {} coins", id, roomIndex, stock, md->price);
            };
            place("vending_machine_i", -4);
            place("water_dispenser", 0);
            place("vending_machine_w", 4);
        }
        // M3e EVENT ROOM (slot 3, MapManager.type3_list = r_mercenary / r_mineral /
        // r_statue). r_mercenary (hire an NPC) is not ported: the slot draws the
        // statue or the mineral room (PORT CHOICE T-13 by floor seed; SK_EVENT_ROOM=
        // statue|mineral overrides for proofs).
        if (slot == 3 && !spawnCells.empty()) {
            const char *forced = std::getenv("SK_EVENT_ROOM");
            const bool statueRoom = forced != nullptr ? std::string(forced) == "statue"
                                                      : ((m_FloorSeed / 5) % 2 == 0);
            const int cx = (rg.Width() - 1) / 2;
            const int cy = (rg.Height() - 1) / 2;
            // statue_0N: one statue per room, N by seed. statue_index runs 1..7
            // (shop.json "statues", FAITHFUL: statue_0N.prefab TalkStatue).
            const int idx = 1 + ((m_FloorSeed / 7) % 7 + 7) % 7;
            const StatueDef *sd = m_Data.FindStatue(idx);
            if (statueRoom && sd == nullptr) {
                LOG_INFO("Statue: index {} missing in shop.json statues", idx);
            } else if (statueRoom) {
                // base pedestal on the cell, body at +body_dy units, light at
                // +light_dy (the prefab's own child offsets, FAITHFUL).
                m_Statue.index = idx;
                m_Statue.cost = sd->cost;
                m_Statue.pos = Room::CellToWorld(cx, cy, rg.Width(), rg.Height(), kCellPx, origin);
                // FAITHFUL @ statue_01.prefab: pedestal layer 4, the god `statue` layer 6
                // (over the heroes), glow `light` layer 5; pivots (0.5,0.1) / (0.6,0) / (0.5,0).
                auto base = addObjSprite(m_SkinCache.count(sd->baseSprite) != 0
                                             ? m_SkinCache[sd->baseSprite]
                                             : (m_SkinCache[sd->baseSprite] = roomImage(
                                                    root + "/sprites/" + sd->baseSprite + ".png")),
                                         sd->baseSprite, cx, cy, sd->baseSort);
                auto bodyImg = roomImage(root + "/sprites/" + sd->bodySprite + ".png");
                addObjSprite(bodyImg, sd->bodySprite, cx, cy, sd->bodySort, sd->bodyDy);
                auto lightImg = roomImage(root + "/sprites/" + sd->lightSprite + ".png");
                addObjSprite(lightImg, sd->lightSprite, cx, cy, sd->lightSort, sd->lightDy);
                // P2.18 (RC4): the statue prefab's `collider` child is a 2x2-unit box at (0, 1) --
                // FOUR cells, not the one the port had (statue_0N.prefab, shop.json blockers).
                const std::optional<Util::Collider> statueCol = FirstBlocker(sd->blockers, m_Statue.pos, kCellPx);
                m_RoomObstacles.push_back(ObstacleBox{
                    statueCol.has_value() ? *statueCol
                                          : Util::Collider::MakeAABB(m_Statue.pos, glm::vec2{kCellPx, kCellPx}),
                    RGBox{}, nullptr, false, true, 0});
                m_Statue.tag = std::make_shared<Util::Text>(root + "/fonts/pixel_bold.ttf", 14,
                                                            std::string(kStatueName[idx]) + "  " +
                                                                std::to_string(sd->cost) + " G",
                                                            Util::Color{255, 230, 120, 255});
                auto tagObj = std::make_shared<Util::GameObject>();
                tagObj->SetDrawable(m_Statue.tag);
                tagObj->m_Transform.translation = m_Statue.pos + glm::vec2(0.0F, 2.6F * kCellPx);
                tagObj->SetZIndex(RenderLayout::kZStatueTag);
                m_RoomTiles.push_back(tagObj);
                m_Renderer.AddChild(tagObj);
                LOG_INFO("Event room {}: statue_{:02d} ({}) -- worship for {} coins", roomIndex, idx,
                         kStatueName[idx], sd->cost);
            } else {
                // r_mineral: mineral_crystal / mineral_gold (res/enemy, EnemyAI05 shells:
                // hp 40, mineral_count 10). The mineral_obj drop prefab did not
                // resolve -> PORT CHOICE T-15: 10 copper coins per mineral. Six ore nodes in a
                // ring (the prefab has no slots; MapManager places them at run time).
                const int ring[6][2] = {{-3, 2}, {3, 2}, {-3, -2}, {3, -2}, {0, 3}, {0, -3}};
                for (int k = 0; k < 6; ++k) {
                    const int mx = cx + ring[k][0];
                    const int my = cy + ring[k][1];
                    if (mx < 1 || my < 1 || mx >= rg.Width() - 1 || my >= rg.Height() - 1 ||
                        Room::IsSolidCell(rg.At(mx, my))) {
                        continue;
                    }
                    const bool gold = (k % 2) == 1;
                    const std::string stem = gold ? "objects1_77" : "objects1_80";
                    auto img = roomImage(root + "/sprites/" + stem + ".png");
                    addShadow(shadowImg2, "shadow2", mx, my, SortKey{RenderLayout::kLayerCharacter, 1});
                    // mineral_gold/crystal body is layer 5 in the prefab; kept in the Wall band
                    // (RoomSortZ maps the actors' layer there) so the hero never sorts randomly.
                    auto tile = addObjSprite(img, stem, mx, my, SortKey{RenderLayout::kLayerCharacter, 1});
                    ObstacleBox ob;
                    ob.collider = Util::Collider::MakeAABB(
                        Room::CellToWorld(mx, my, rg.Width(), rg.Height(), kCellPx, origin),
                        glm::vec2{kCellPx, kCellPx});
                    ob.tile = tile;
                    ob.destructible = true;
                    ob.box.SetHp(40); // FAITHFUL: ra_max_hp 40
                    ob.box.SetSeed(m_FloorSeed + 1 + roomIndex + k);
                    ob.breakCoins = 10; // FAITHFUL count: mineral_count 10
                    m_RoomObstacles.push_back(std::move(ob));
                }
                LOG_INFO("Event room {}: mineral room (6 ore nodes, hp 40, 10 coins each)", roomIndex);
            }
        }
        if (slot == 4 && !sellRoom && !spawnCells.empty()) {
            const auto &cc = spawnCells[spawnCells.size() / 2];
            const glm::vec2 cpos = Room::CellToWorld(
                cc.first, cc.second, rg.Width(), rg.Height(), kCellPx, origin);
            auto chest = std::make_shared<Chest>(
                root, cpos, m_Level != nullptr ? m_Level->chestLevel : 1, Chest::Kind::RoomReward);
            // NOT PORTED T-09: GetPot/GetCoin branches and ChestXmlReader; reward chests
            // only yield weapons until P1.9 resolves them.
            m_Chests.push_back(chest);
            m_Renderer.AddChild(chest);
            LOG_INFO("Chest: reward room {} -> chest_room_reward (tier {})", roomIndex, chest->Tier());
        }
        ++roomIndex;
    }

    // --- Player continuation (Route (a)) ---
    // Overwrite the template vitals with the carried snapshot (empty on floor 0),
    // then equip the carried weapon (floor 0 -> default Gun001). This MIRRORS the
    // live pickup-equip below: on EVERY equip, set m_CurrentWeaponId AND re-derive
    // m_WeaponEnergyCost via WeaponEnergyCost() -- the firing gate + energy spend
    // read it and it defaults to 1, so a carried weapon whose consume != 1 would
    // mis-cost the whole floor if this DERIVE were skipped (RUN_LOOP_PLAN D3).
    if (m_Carried) {
        m_Player->Stats() = m_Carried->stats; // continue hp/armor/energy (Player.hpp:33 mutable ref)
    }
    std::string equipWeaponId =
        m_Carried ? m_Carried->weaponId : std::string("Gun001");
    // SK_FORCE_WEAPON test hook (env, NO-OP if unset): equip this weapon id/class
    // instead of the carried/default one at floor start -- headless proof-of-life
    // for weapon-specific behaviour (e.g. P3.2 bullet buffs) without a full pickup
    // walk. FindWeapon resolves either an exact prefab id (weapon_013) or a class
    // id (Gun001..); an unresolvable value is silently ignored (keeps the normal
    // equip).
    if (const char *fw = std::getenv("SK_FORCE_WEAPON")) {
        if (m_Data.FindWeapon(fw) != nullptr) {
            equipWeaponId = fw;
            LOG_INFO("SK_FORCE_WEAPON -> equipping '{}'", equipWeaponId);
        }
    }
    if (const WeaponDef *wdef = m_Data.FindWeapon(equipWeaponId)) {
        m_Sim->EquipWeapon(*wdef, equipWeaponId, m_FloorSeed + 5, &m_Data); // P3.2: bullet-buff wiring
        m_WeaponEnergyCost = WeaponEnergyCost(*wdef);
        // AUDIO: wdef->id is the RESOLVED prefab id (e.g. "weapon_000"), unlike
        // equipWeaponId which may still be a class id ("Gun001") -- audio.json
        // weapon_shoot is keyed by prefab id, same as the live pickup-equip path
        // below (WeaponPickup::Def()->id), so this keeps the carry vehicle
        // consistent and WeaponShootClip() resolvable from floor 0.
        m_CurrentWeaponId = wdef->id;
    } // else: unknown id -> leave the default (never deref a null FindWeapon).

    // --- B1-P4a/P3.3.b: dispatch the player skill brain by character id (charId
    // seam, MakeCharSkill in CharSkillAdapters.hpp). skillCd / inSkillTime / atk come
    // from the resolved CharacterDef (cdef, per-char stats -- see characters.json),
    // falling back to the shared PlayerTemplate only when cdef is null. charId is
    // carried across floors (defaults "c01" until the HeroRoom picker, B5). C01,
    // C02, C03, C04, C05, C07, C08, C09, C10, C11, C12 and C13 have real per-hero
    // bodies (P3.3.c added C03/C05/C07/C11/C12); only C06 remains a gate+cooldown-only
    // stub (StubSkillAdapter). The CharacterDef itself is passed through so heroes that
    // read effect-prefab data (C05 bottle variants, C07 BulletBat.damage, C12 crit) take
    // it from characters.json's skill_refs/skill_params. Rebuilt per floor (starts ready).
    // charId + cdef were resolved at player-creation time (top of OnEnter).
    m_Sim->SetPlayerSkill(charId,
                          cdef != nullptr ? cdef->skillCd : m_Data.PlayerTemplate().skillCd,
                          cdef != nullptr ? cdef->inSkillTime
                                          : m_Data.PlayerTemplate().inSkillTime,
                          cdef != nullptr ? cdef->atk : m_Data.PlayerTemplate().atk, cdef);

    // --- Forward run loop (step 2a) setup ---
    // Arm the clear check only after >= 1 hostile has existed, read from the sim's
    // AUTHORITATIVE state (not the render mirrors). Mob floors (no boss) arm via the
    // EnemyViews() term -- every non-start room spawns one mob -- so the boss is NOT
    // the load-bearing armer; the guard stops a degenerate floor from auto-skipping.
    m_HadHostiles = m_Sim->HasBoss() || !m_Sim->EnemyViews().empty();
    // SK_FORCE_CLEAR=K test hook (env, NO-OP if unset): force the clear path on frame
    // K of THIS floor, to drive the play->clear->next-floor transition headlessly.
    if (const char *fc = std::getenv("SK_FORCE_CLEAR")) {
        m_ForceClearFrame = std::atol(fc);
    }
    if (const char *fd = std::getenv("SK_FORCE_DIE")) {
        m_ForceDieFrame = std::atol(fd);
    }
    if (const char *fk = std::getenv("SK_FORCE_KILL")) {
        m_ForceKillFrame = std::atol(fk);
    }
    if (const char *fe = std::getenv("SK_FORCE_KILL_EVERY")) {
        m_ForceKillEvery = std::atol(fe); // repeat the kill every N frames after the first
    }
    if (const char *gt = std::getenv("SK_GATE_TELEPORT")) {
        m_GateTeleportFrame = std::atol(gt);
    }
    if (const char *gb = std::getenv("SK_GATE_BESIDE")) {
        m_GateBesideFrame = std::atol(gb);
    }
    if (const char *sb = std::getenv("SK_SHOP_BUY")) {
        m_ShopBuyFrame = std::atol(sb);
    }
    if (const char *sw = std::getenv("SK_STATUE_WORSHIP")) {
        m_StatueWorshipFrame = std::atol(sw);
    }
    if (const char *sc = std::getenv("SK_COINS")) {
        if (m_Run != nullptr) {
            m_Run->AddCoins(std::atoi(sc)); // test aid: seed the run's coins
        }
    }
    if (const char *rt = std::getenv("SK_ROOM_TELEPORT")) {
        // SK_ROOM_TELEPORT=K,R[,dx,dy] (strict parse, see game/TestHooks.hpp). The pixel
        // offset (e.g. dy=-160 stands the player below the boss instead of on it) applies
        // only to the centre landing (boss / gate rooms). Invalid -> hook stays unset.
        const Game::RoomTeleportSpec spec = Game::ParseRoomTeleport(rt);
        if (spec.ok) {
            m_RoomTeleportFrame = spec.frame;
            m_RoomTeleportRoom = spec.room;
            m_RoomTeleportDx = spec.dx;
            m_RoomTeleportDy = spec.dy;
        } else {
            LOG_WARN("SK_ROOM_TELEPORT rejected: {}", spec.error);
        }
    }
    // P2.18 evidence hooks (strict parse; invalid -> the hook stays unset). NO-OP unless set.
    if (const char *fd = std::getenv("SK_FORCE_DASH")) {
        const Game::ForceDashSpec spec = Game::ParseForceDash(fd);
        if (spec.ok) {
            m_ForceDashFrame = spec.frame;
            m_ForceDashDir = glm::vec2{spec.dx, spec.dy};
        } else {
            LOG_WARN("SK_FORCE_DASH rejected: {}", spec.error);
        }
    }
    if (const char *wd = std::getenv("SK_WALK_DIR")) {
        const Game::WalkDirSpec spec = Game::ParseWalkDir(wd);
        if (spec.ok) {
            m_WalkDirSet = true;
            m_WalkDir = glm::vec2{spec.dx, spec.dy};
        } else {
            LOG_WARN("SK_WALK_DIR rejected: {}", spec.error);
        }
    }
    if (const char *tp = std::getenv("SK_TRACE_POS")) {
        const Game::FrameListSpec spec = Game::ParseFrameList(tp);
        if (spec.ok) {
            m_TraceFrames = spec.frames;
        } else {
            LOG_WARN("SK_TRACE_POS rejected: {}", spec.error);
        }
    }
    if (const char *fs = std::getenv("SK_FORCE_SKILL")) {
        m_ForceSkillFrame = std::atol(fs);
    }
    // item 6 (H12): SK_FORCE_SKILL_PULSE=K1,K2 -- see the member doc comment
    // (GameScene.hpp) for why this is a SEPARATE hook from SK_FORCE_SKILL (a held
    // press vs. two one-frame pulses).
    if (const char *fsp = std::getenv("SK_FORCE_SKILL_PULSE")) {
        m_ForceSkillPulseFrame1 = std::atol(fsp);
        if (const char *comma = std::strchr(fsp, ',')) {
            m_ForceSkillPulseFrame2 = std::atol(comma + 1);
        }
    }
    // SK_FORCE_HIT=K test hook (env, NO-OP if unset): K is the FIRST frame the
    // hook acts on, not a one-shot -- from frame K onward it re-aims at the
    // nearest enemy/boss and holds fire EVERY frame (see the aim block in
    // ResolveInput and m_ForceHitFrame's doc comment).
    if (const char *fh = std::getenv("SK_FORCE_HIT")) {
        m_ForceHitFrame = std::atol(fh);
    }
    // SK_FORCE_BUFF=K,<kind> test hook (env, NO-OP if unset): apply <kind> to the
    // player on frame K via Simulation::ApplyBuff, so the "Buff: ... applied/ended"
    // log path (P3.1) can be proven headlessly without bullet-trigger wiring (P3.2).
    if (const char *fb = std::getenv("SK_FORCE_BUFF")) {
        m_ForceBuffFrame = std::atol(fb);
        if (const char *comma = std::strchr(fb, ',')) {
            m_ForceBuffKind = comma + 1;
        }
    }
    // SK_AUTOFIRE test hook (env, NO-OP if unset): when also auto-walking, fire along
    // the travel direction (see Update). Kept separate so SK_AUTOWALK stays pure
    // navigation -- no firing, no energy spend.
    if (std::getenv("SK_AUTOFIRE") != nullptr) {
        m_AutoFire = true;
    }
    // SK_AUTOWALK test hook (env, NO-OP if unset): steer the player straight at an
    // orthogonally-adjacent room's centre each frame so the play->corridor->room
    // traversal can be exercised + logged headlessly (the start room's centre is
    // the world origin, so adjacent room centres sit at +/-kRoomPitch on one axis).
    if (std::getenv("SK_AUTOWALK") != nullptr) {
        for (std::size_t i = 0; i < m_Rooms.size(); ++i) {
            if (static_cast<int>(i) == floor.StartIndex()) {
                continue;
            }
            const glm::vec2 c = m_Rooms[i]->Center();
            const bool adj =
                (std::abs(std::abs(c.x) - kRoomPitch) < 1.0F && std::abs(c.y) < 1.0F) ||
                (std::abs(c.x) < 1.0F && std::abs(std::abs(c.y) - kRoomPitch) < 1.0F);
            if (adj) {
                m_AutoWalk = true;
                m_AutoWalkTarget = c;
                m_AutoWalkRoom = static_cast<int>(i);
                break;
            }
        }
        LOG_INFO("SK_AUTOWALK -> target room={} centre=({:.0f},{:.0f}) startSpawn=({:.0f},{:.0f})",
                 m_AutoWalkRoom, m_AutoWalkTarget.x, m_AutoWalkTarget.y,
                 startFloorPos.x, startFloorPos.y);
    }
    if (m_Carried) {
        LOG_INFO("Floor {} continuation LOADED: hp={} armor={} energy={} weapon='{}' (seed={})",
                 m_FloorIndex, m_Player->Stats().hp, m_Player->Stats().armor,
                 m_Player->Stats().energy, m_CurrentWeaponId, m_FloorSeed);
    } else {
        LOG_INFO("Floor {} fresh start (template) hp={} weapon='{}' (seed={})", m_FloorIndex,
                 m_Player->Stats().hp, m_CurrentWeaponId, m_FloorSeed);
    }

    m_Renderer.AddChild(m_Player); // enemies register in SpawnEnemy (MAP M1 waves)
    for (const auto &boss : m_Bosses) {
        m_Renderer.AddChild(boss);
    }

    m_Player->m_Transform.translation = startFloorPos;
    m_Camera.SetPosition(m_Player->Position());
    // D: zoom in so the character occupies a reasonable fraction of the screen (the
    // c01 sprite is 32x29 px; at zoom 1 it was ~2.5% of a 1280px view -- a tiny dot).
    // Zoom 2.5 -> ~80px tall + shows ~512px of world (about one 15-cell room), matching
    // the original's framing. A camera-DISTANCE change: the whole world->screen ratio
    // scales together, so player/enemy/tile relative sizes stay consistent. Feel value.
    // FAITHFUL @ Scene_Game.unity Main Camera: orthographic size 7.5 -> the view is
    // 15 world units tall -> 48 px per unit at 720p. Cells are kCellPx (32) px per
    // unit, so zoom = 48 / 32 = 1.5 (was 2.5: the map showed only 16 units across
    // instead of the original's 26.7).
    float zoom = 48.0F / kCellPx;
    // SK_ZOOM test hook (env, NO-OP if unset): override the camera zoom for
    // headless whole-room proof screenshots only; default behaviour is unchanged.
    if (const char *zv = std::getenv("SK_ZOOM")) {
        zoom = static_cast<float>(std::atof(zv));
        LOG_INFO("GameScene: SK_ZOOM override {}", zoom);
    }
    m_Camera.SetZoom(zoom);
    LOG_INFO("SpawnFilter: {} of {} connected floor cells removed from the spawn pools (inside a box / brazier / "
             "prop / wall collider for a {:.1f} px body)", m_SpawnCellsRemoved, m_SpawnCellsTotal, m_PlayerRadiusPx);
    LOG_INFO("GameScene: multi-room floor ready");
}

void GameScene::OnExit() {
    // Fires when the SceneManager pops/replaces this scene (DoPop -> OnExit before
    // destruction). Logging it proves the dying floor is EXITED on a death/clear
    // Replace -- not Pushed-over-and-leaked at the bottom of the stack (step 2b).
    LOG_INFO("Floor {} GameScene OnExit (scene torn down, not leaked)", m_FloorIndex);
    glClearColor(0.0F, 0.0F, 0.0F, 1.0F); // menus / the next scene start from black again (P2.17 c)
}

glm::vec2 GameScene::AimDirection() const {
    // FIX (aim never tracked the mouse): Util::Input::GetCursorPosition() ALREADY returns
    // PTSD coords -- center-origin, Y-up -- i.e. the cursor's offset from the screen centre
    // in screen px (Input::Update calls FromSDL once, Input.cpp:80). The old code applied
    // FromSDL a SECOND time, giving w.x = sdlx - WINDOW_WIDTH (always <= 0) and w.y = sdly
    // (Y un-flipped), which pinned the aim to the up-left quadrant regardless of the mouse.
    // Use the cursor directly.
    const Util::PTSDPosition cursor = Util::Input::GetCursorPosition();
    const float zoom = m_Camera.GetZoom() > 0.0F ? m_Camera.GetZoom() : 1.0F;
    // The camera centre maps to the screen centre; 1 world px renders as `zoom` screen px,
    // so a screen-px cursor offset is `offset / zoom` world px about the camera centre ->
    // the world point under the cursor.
    const glm::vec2 worldCursor =
        m_Camera.GetPosition() + glm::vec2(cursor.x, cursor.y) / zoom;
    return Normalize(worldCursor - m_Player->Position());
}

bool GameScene::BlocksAny(glm::vec2 pos, float radius) const {
    for (const auto &room : m_Rooms) {
        if (room->Blocks(pos, radius)) {
            return true;
        }
    }
    // Corridor flank walls (permanent: the strip is walled on its long sides and
    // open only at its two ends -- the room door and the neighbour's corridor).
    if (!m_Corridors.empty()) {
        const Util::Collider circle = Util::Collider::MakeCircle(pos, radius);
        for (const Util::Collider &wall : m_Corridors) {
            if (Util::Overlap(wall, circle)) {
                return true;
            }
        }
    }
    // Phase 3 design obstacles (boxes + braziers): block bodies while alive. A box
    // stops blocking once broken (DamageObstacle sets alive=false). Off the
    // connectivity grid, so they block bodies without breaking door reachability.
    if (!m_RoomObstacles.empty()) {
        const Util::Collider circle = Util::Collider::MakeCircle(pos, radius);
        for (const ObstacleBox &ob : m_RoomObstacles) {
            if (ob.alive && Util::Overlap(ob.collider, circle)) {
                return true;
            }
        }
    }
    // Sealed doors: a room seals its door opening EXACTLY while its RGRoomX has
    // door_open==false (Active/locked). This is a pure VIEW of RGRoomX.door_open
    // -- the SINGLE authority (#3a); there is no separate writable lock field. A
    // room is sealed only after StartRoom (Active) and reopens on ClearRoom.
    {
        const Util::Collider circle = Util::Collider::MakeCircle(pos, radius);
        for (std::size_t i = 0;
             i < m_RoomDoors.size() && i < m_RoomLife.size(); ++i) {
            if (m_RoomLife[i].DoorOpen()) {
                continue; // doors open -> not sealed
            }
            for (const Util::Collider &door : m_RoomDoors[i]) {
                if (Util::Overlap(door, circle)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void GameScene::GatherBlockersNear(glm::vec2 pos, float reach, std::vector<Util::Collider> &out) const {
    const auto within = [&](const Util::Collider &c) {
        const glm::vec2 lo = c.Min();
        const glm::vec2 hi = c.Max();
        return hi.x >= pos.x - reach && lo.x <= pos.x + reach && hi.y >= pos.y - reach &&
               lo.y <= pos.y + reach;
    };
    for (const auto &room : m_Rooms) {
        for (const Util::Collider &wall : room->Walls()) {
            if (within(wall)) {
                out.push_back(wall);
            }
        }
    }
    for (const Util::Collider &wall : m_Corridors) {
        if (within(wall)) {
            out.push_back(wall);
        }
    }
    for (const ObstacleBox &ob : m_RoomObstacles) {
        if (ob.alive && within(ob.collider)) {
            out.push_back(ob.collider);
        }
    }
    for (std::size_t i = 0; i < m_RoomDoors.size() && i < m_RoomLife.size(); ++i) {
        if (m_RoomLife[i].DoorOpen()) {
            continue; // doors open -> not sealed (same rule as BlocksAny)
        }
        for (const Util::Collider &door : m_RoomDoors[i]) {
            if (within(door)) {
                out.push_back(door);
            }
        }
    }
}

// P2.18 (RC1, PORT CHOICE T-61): push a body that overlaps a blocker out to the nearest free
// position. One BlocksAny() query when nothing overlaps (the common case). Otherwise the blockers
// within reach (radius + 3 cells: a 2x2 statue and the neighbouring cells a candidate exit may
// land in) are gathered and solved exactly (circle-vs-AABB / circle minimum translation, see
// sim/Depenetration.hpp); if that solve cannot leave the pocket or its result still collides with
// something outside the gathered set, the generic sampled search over Blocks() finishes the job.
glm::vec2 GameScene::Depenetrate(glm::vec2 pos, float radius) const {
    if (!BlocksAny(pos, radius)) {
        return pos;
    }
    std::vector<Util::Collider> nearby;
    GatherBlockersNear(pos, radius + 3.0F * kCellPx, nearby);
    bool resolved = true;
    const glm::vec2 out = Sim::DepenetrateAgainst(nearby, pos, radius, &resolved);
    if (resolved && !BlocksAny(out, radius)) {
        return out;
    }
    return WorldCollision::Depenetrate(pos, radius);
}

// P2.18 (R37, T-62): exact swept test. The blockers whose bounds lie within reach of the
// segment's bounding circle are gathered (same collections as BlocksAny, active ones only) and the
// segment is tested against the exact Minkowski sum of each; a graze between two free end points
// is caught. A zero-length segment is the plain overlap test.
bool GameScene::MoveBlocked(glm::vec2 from, glm::vec2 to, float radius) const {
    const glm::vec2 mid = 0.5F * (from + to);
    const glm::vec2 half = 0.5F * (to - from);
    const float reach = std::sqrt(half.x * half.x + half.y * half.y) + radius + 1.0F;
    static thread_local std::vector<Util::Collider> nearby; // reused: this runs twice per mover per tick
    nearby.clear();
    GatherBlockersNear(mid, reach, nearby);
    return Sim::SegmentBlockedByAny(nearby, from, to, radius);
}

bool GameScene::SpawnBlocked(std::size_t room, glm::vec2 pos, float radius) const {
    if (BlocksAny(pos, radius)) {
        return true;
    }
    // A spawn must also stay clear of the room's door-seal band even while the doors are open:
    // the seal closes on the whole opening the moment the player enters, and a body standing in it
    // would be pushed out of the room.
    if (room < m_RoomDoors.size()) {
        const Util::Collider circle = Util::Collider::MakeCircle(pos, radius);
        for (const Util::Collider &door : m_RoomDoors[room]) {
            if (Util::Overlap(door, circle)) {
                return true;
            }
        }
    }
    return false;
}

glm::vec2 GameScene::FreeDropPoint(glm::vec2 want) const {
    // A dropped weapon is a point-like pickup: an 8 px probe keeps it off boxes, walls and doors.
    return Depenetrate(want, 8.0F);
}

void GameScene::TracePositions() const {
    LOG_INFO("PosTrace: frame {} player ({:.1f},{:.1f}) r={:.1f} blocked={}", m_Frame,
             m_Player->Position().x, m_Player->Position().y, m_PlayerRadiusPx,
             BlocksAny(m_Player->Position(), m_PlayerRadiusPx) ? 1 : 0);
    const std::vector<Sim::Simulation::EntityView> evs = m_Sim->EnemyViews();
    for (const Sim::Simulation::EntityView &ev : evs) {
        const float r = ev.id < m_EnemyDefs.size() && m_EnemyDefs[ev.id] != nullptr
                            ? Sim::MakeBodyGeom(m_EnemyDefs[ev.id]->body, m_EnemyDefs[ev.id]->hurtbox,
                                                Sim::kEnemyBodyRadius).bodyRadiusPx
                            : Sim::kEnemyBodyRadius;
        LOG_INFO("PosTrace: frame {} enemy {} room {} {} ({:.1f},{:.1f}) r={:.1f} alive={} blocked={}", m_Frame,
                 ev.id, ev.roomId,
                 ev.id < m_EnemyDefs.size() && m_EnemyDefs[ev.id] != nullptr ? m_EnemyDefs[ev.id]->id : "?",
                 ev.pos.x, ev.pos.y, r, ev.alive ? 1 : 0, BlocksAny(ev.pos, r) ? 1 : 0);
    }
    if (m_Sim->HasBoss()) {
        const Sim::Simulation::EntityView bv = m_Sim->BossView();
        const float r = m_BossDef != nullptr
                            ? Sim::MakeBodyGeom(m_BossDef->body, m_BossDef->hurtbox, Sim::kBossBodyRadius).bodyRadiusPx
                            : Sim::kBossBodyRadius;
        LOG_INFO("PosTrace: frame {} boss room {} ({:.1f},{:.1f}) r={:.1f} alive={} blocked={}", m_Frame,
                 bv.roomId, bv.pos.x, bv.pos.y, r, bv.alive ? 1 : 0, BlocksAny(bv.pos, r) ? 1 : 0);
    }
}

// Phase 3 (#3a): a consumed bullet at @p pos damages a destructible design box
// there (no-op for walls / permanent braziers / already-broken boxes). Once a box
// breaks it stops blocking (alive=false) and its placeholder tile is hidden. RGBox
// is zero-RNG, so this never perturbs determinism; under NullWorldCollision (the
// combat goldens) Blocks never fires, so this is never reached -> combat hash safe.
void GameScene::DamageObstacle(glm::vec2 pos, float radius) {
    const Util::Collider circle = Util::Collider::MakeCircle(pos, radius);
    for (ObstacleBox &ob : m_RoomObstacles) {
        if (!ob.alive || !ob.destructible) {
            continue; // already broken, or a permanent brazier
        }
        if (!Util::Overlap(ob.collider, circle)) {
            continue;
        }
        const RGBox::HitResult r = ob.box.Hit(1, /*sourceValid=*/true);
        if (r.registered && r.shouldDestroy) {
            ob.alive = false; // stops blocking (passable)
            if (ob.breakCoins > 0) {
                // M3e mineral node: its ore (mineral_count) as copper coins.
                const std::string rootDir(RESOURCE_DIR);
                for (int c = 0; c < ob.breakCoins; ++c) {
                    const float ang = m_RewardRng.Range(0.0F, 6.2831853F);
                    const PickupDef *pd = m_Data.FindPickup("copper");
                    if (pd == nullptr) {
                        break;
                    }
                    auto coin = std::make_shared<CoinPickup>(rootDir, ob.collider.center, RewardKind::Copper,
                                                             glm::vec2{std::cos(ang), std::sin(ang)}, *pd,
                                                             m_Data.CoinFriction(), kScatterImpulseUnits);
                    m_Coins.push_back(coin);
                    m_Renderer.AddChild(coin);
                }
                LOG_INFO("Mineral: broke -> {} copper coins", ob.breakCoins);
            }
            if (ob.tile != nullptr) {
                ob.tile->SetVisible(false); // hide the broken box's placeholder skin
            }
            if (ob.top != nullptr) {
                ob.top->SetVisible(false); // ... and its w2 lid (now drawn over the heroes, P2.17)
            }
            LOG_INFO("RGBox: design box broken at ({:.0f},{:.0f}) -> now passable",
                     pos.x, pos.y);
        }
        break; // one box per bullet impact
    }
}

void GameScene::SyncBulletViews() {
    const std::vector<Sim::BulletState> &bullets = m_Sim->Bullets();
    std::unordered_set<std::uint32_t> present;
    present.reserve(bullets.size());
    for (const Sim::BulletState &b : bullets) {
        present.insert(b.id);
    }
    // Release views whose sim bullet is gone.
    for (auto it = m_BulletViews.begin(); it != m_BulletViews.end();) {
        if (present.find(it->first) == present.end()) {
            m_Renderer.RemoveChild(it->second);
            it->second->Deactivate();
            m_BulletPool.Release(it->second);
            it = m_BulletViews.erase(it);
        } else {
            ++it;
        }
    }
    // Acquire new views; mirror every live bullet's position.
    for (const Sim::BulletState &b : bullets) {
        auto found = m_BulletViews.find(b.id);
        std::shared_ptr<Bullet> view;
        if (found == m_BulletViews.end()) {
            view = m_BulletPool.Acquire();
            view->Init(b.pos, b.vel, kBulletLifeMs, b.damage, b.camp); // arms the sprite; not Update()d.
            // WEAPON RE-KEY: a player bullet wears its weapon's real bullet art
            // (weapons.json bullet_sprite); enemy/unknown keeps the bullet_0
            // default. Set on every acquire: pooled views are reused across weapons.
            std::string bulletPng = "bullet_0.png";
            if (b.camp == 0) {
                if (const WeaponDef *w = m_Data.FindWeapon(m_CurrentWeaponId);
                    w != nullptr && !w->bulletSprite.empty()) {
                    bulletPng = w->bulletSprite;
                }
            }
            view->SetSprite(std::string(RESOURCE_DIR) + "/sprites/" + bulletPng);
            m_Renderer.AddChild(view);
            m_BulletViews.emplace(b.id, view);
        } else {
            view = found->second;
        }
        view->m_Transform.translation = b.pos;
    }
}

bool GameScene::RoomHasLiveHostile(int roomId) const {
    for (const Sim::Simulation::EntityView &ev : m_Sim->EnemyViews()) {
        if (ev.alive && ev.roomId == roomId) {
            return true;
        }
    }
    if (m_Sim->HasBoss()) {
        const Sim::Simulation::EntityView bv = m_Sim->BossView();
        if (bv.alive && bv.roomId == roomId) {
            return true;
        }
    }
    return false;
}

void GameScene::Update(float dtMs) {
    if (Util::Input::IsKeyUp(Util::Keycode::ESCAPE) || Util::Input::IfExit()) {
        Core::Context::GetInstance()->SetExit(true);
        return;
    }

    ++m_Frame; // drives the SK_FORCE_CLEAR / SK_FORCE_DIE test hooks.
    if (m_Run != nullptr) {
        m_Run->RecordTime(dtMs); // B4: run clock (RGGameProcess.start_time analog)
    }
    // P2.18 (RC3, PORT CHOICE T-62): a frame hitch must slow the game down, not teleport the hero.
    // The run clock above keeps the wall-clock delta; everything frame-driven below (player
    // motion, regeneration, timers, the sim's own Advance) sees at most kMaxFrameDtMs (= the
    // 8 fixed steps FixedClock already allows per Advance), so no frame moves the player more
    // than 33.3 px (6.5 u/s * 32 px * 0.16 s) -- and MoveBody sub-steps even that.
    {
        static int clampLogs = 0;
        if (dtMs > Sim::kMaxFrameDtMs && clampLogs < 3) {
            ++clampLogs;
            LOG_INFO("Frame dt {} ms clamped to {} ms (frame {})", dtMs, Sim::kMaxFrameDtMs, m_Frame);
        }
        m_RawMsTotal += dtMs; // diagnostics for the DeathTrace line (H3 A/B)
        dtMs = Sim::ClampFrameDtMs(dtMs);
        m_SimMsTotal += dtMs;
    }
    // SK_FORCE_DIE=K test hook (env, NO-OP if unset): kill the player on frame K to
    // drive the death->EndScene path headlessly (no combat needed). Runs BEFORE the
    // death check below.
    if (m_ForceDieFrame >= 0 && m_Frame >= m_ForceDieFrame) {
        m_Player->Stats().hp = 0;
    }
    if (m_ForceKillFrame >= 0 && m_Sim &&
        (m_Frame == m_ForceKillFrame ||
         (m_ForceKillEvery > 0 && m_Frame > m_ForceKillFrame &&
          (m_Frame - m_ForceKillFrame) % m_ForceKillEvery == 0))) {
        m_Sim->KillAllHostiles(); // SK_FORCE_KILL[_EVERY] test hook: NO-OP unless set
    }
    if (m_GateTeleportFrame >= 0 && m_Frame == m_GateTeleportFrame) {
        m_Player->m_Transform.translation = m_GatePos; // SK_GATE_TELEPORT test hook
    }
    if (m_RoomTeleportFrame >= 0 && m_Frame == m_RoomTeleportFrame && m_RoomTeleportRoom >= 0 &&
        static_cast<std::size_t>(m_RoomTeleportRoom) < m_Waves.size()) {
        const auto &pts = m_Waves[static_cast<std::size_t>(m_RoomTeleportRoom)].points;
        if (!pts.empty()) {
            m_Player->m_Transform.translation = pts[pts.size() / 2]; // SK_ROOM_TELEPORT
            m_Camera.SetPosition(m_Player->Position()); // snap: proof shots may land on this frame
            LOG_INFO("GameScene: SK_ROOM_TELEPORT -> room {} spawn point", m_RoomTeleportRoom);
        } else if (m_RoomTeleportRoom == m_GateRoom) {
            // Level-0 makers (boss / gate rooms) have no spawn points: use the
            // room's centre cell, which the gate placement already computed.
            m_Player->m_Transform.translation =
                m_GatePos + glm::vec2(m_RoomTeleportDx, m_RoomTeleportDy);
            m_Camera.SetPosition(m_Player->Position());
            LOG_INFO("GameScene: SK_ROOM_TELEPORT -> room {} centre (no spawn points) at ({},{})",
                     m_RoomTeleportRoom, m_GatePos.x, m_GatePos.y);
            if (m_RoomTeleportDx != 0.0F || m_RoomTeleportDy != 0.0F) {
                LOG_INFO("GameScene: SK_ROOM_TELEPORT offset ({},{})", m_RoomTeleportDx,
                         m_RoomTeleportDy);
            }
        } else {
            LOG_INFO("GameScene: SK_ROOM_TELEPORT -> room {} skipped (no spawn points)",
                     m_RoomTeleportRoom);
        }
    }
    if (m_GateBesideFrame >= 0 && m_Frame == m_GateBesideFrame) {
        m_Player->m_Transform.translation = m_GatePos + glm::vec2{0.0F, -64.0F}; // SK_GATE_BESIDE
    }
    // SK_FORCE_BUFF=K,<kind> test hook (env, NO-OP if unset): apply <kind> to the
    // player on frame K via Simulation::ApplyBuff (P3.1). Not bullet-triggered
    // (P3.2) -- a direct API poke so the buff-applied/ended log path is provable
    // headlessly. Unknown kind names are silently ignored (NO-OP).
    if (m_ForceBuffFrame >= 0 && m_Frame == m_ForceBuffFrame && m_Sim) {
        static const std::unordered_map<std::string, std::string> kKindToBuffId = {
            {"fire", "BuffFire"},         {"poison", "BuffPoison"},
            {"ice", "BuffIce"},           {"electric", "BuffElectric"},
            {"armor", "BuffArmor"},       {"changespeed", "BuffChangeSpeed"},
        };
        const auto it = kKindToBuffId.find(m_ForceBuffKind);
        if (it != kKindToBuffId.end()) {
            if (const BuffDef *bdef = m_Data.FindBuff(it->second)) {
                m_Sim->ApplyBuff(Sim::Simulation::kPlayerViewId, Game::BuffParamsFrom(*bdef));
            }
        }
    }

    // --- Player movement with axis-separated wall sliding (stays scene-side) ---
    // P2.18: first resolve any overlap left by a teleport hook, a door that just sealed, a
    // knock-in or a spawn (WorldCollision::Depenetrate, same tick), then walk with the swept
    // MoveBody below.
    const glm::vec2 before = Depenetrate(m_Player->Position(), m_PlayerRadiusPx);
    m_Player->m_Transform.translation = before;
    m_Player->Update(dtMs);
    glm::vec2 after = m_Player->Position();
    if (m_AutoWalk) { // SK_AUTOWALK: override input with a steer toward the target
        const glm::vec2 d = m_AutoWalkTarget - before;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 1.0F) {
            after = before + (d / len) * (kAutoWalkSpeed * dtMs / 1000.0F);
        }
    }
    if (m_WalkDirSet) { // SK_WALK_DIR: hold a fixed direction (P2.18 wall / dt-spike evidence)
        const float len = std::sqrt(m_WalkDir.x * m_WalkDir.x + m_WalkDir.y * m_WalkDir.y);
        if (len > 0.0F) {
            after = before + (m_WalkDir / len) * (m_Player->Speed() * dtMs / 1000.0F);
        }
    }
    // P3.1: the sim owns the player's buff state (TickBuffs, ticked in the PREVIOUS
    // Advance()) but not the player's position -- gate this frame's movement on it.
    // Ice/Electric freeze -> speedMul reaches 0 -> movement is fully blocked;
    // ChangeSpeed's slow scales the travel delta.
    if (m_Sim) {
        if (m_Sim->PlayerFrozen()) {
            after = before; // frozen: block movement entirely.
        } else if (m_Sim->PlayerSpeedMul() != 1.0F) {
            after = before + (after - before) * m_Sim->PlayerSpeedMul();
        }
    }
    // Swept, axis-sliding commit (T-62): same slide as before, but in sub-steps of at most one body
    // radius and each axis move is a swept test, so a step cannot land on the far side of a 1-cell wall.
    glm::vec2 resolved = Sim::MoveBody(*this, before, after - before, m_PlayerRadiusPx);
    // SK_FORCE_DASH=K,dx,dy (P2.18 evidence): the C02 dash applier, driven without the skill.
    if (m_ForceDashFrame >= 0 && m_Frame == m_ForceDashFrame) {
        const glm::vec2 from = resolved;
        Sim::ApplyDash(*this, resolved, m_ForceDashDir, Sim::kSkillDashPx, m_PlayerRadiusPx);
        LOG_INFO("Dash: SK_FORCE_DASH frame {} from ({:.1f},{:.1f}) to ({:.1f},{:.1f}) blocked={}", m_Frame,
                 from.x, from.y, resolved.x, resolved.y, BlocksAny(resolved, m_PlayerRadiusPx) ? 1 : 0);
    }
    m_Player->m_Transform.translation = resolved;

    if (!m_TraceFrames.empty() &&
        std::find(m_TraceFrames.begin(), m_TraceFrames.end(), static_cast<long>(m_Frame)) != m_TraceFrames.end()) {
        TracePositions(); // SK_TRACE_POS (P2.18 evidence)
    }

    // --- Energy regen (scene-owned, decision C) ---
    // FAITHFUL: drive the decomp-grounded 2.0s cadence via CombatStats::EnergyReloadTick
    // (RoleAttributePlayer__EnergyReLoad @ game_full.c:432415) -- one tick per frame, +1
    // energy at the 2.0s boundary -- instead of the old hand-rolled 400ms accumulator,
    // which regenerated energy 5x too fast. The accumulator now lives in CombatStats.energyTime.
    m_Player->Stats().EnergyReloadTick(dtMs / 1000.0F);

    // --- Which room is the player in (awake gating + clear-room) ---
    int playerRoomId = -1;
    for (std::size_t i = 0; i < m_Rooms.size(); ++i) {
        // Seam 2 (corridors are neutral): a corridor position lies in the
        // inter-block margin, inside NO room rect, so playerRoomId stays -1 there
        // and the clear-room door seal never arms while the player is in a
        // corridor. Same Room::ContainsPoint the neutrality test asserts.
        if (m_Rooms[i]->ContainsPoint(resolved)) {
            playerRoomId = static_cast<int>(i);
            break; // player is in exactly one room
        }
    }

    // --- Drive the deterministic sim ---
    // A3 skill input: skill/ultimate on MOUSE_RB (edge-triggered, like a press). NOTE:
    // no original PC skill key is recoverable -- Soul Knight is mobile-origin (the skill
    // is an on-screen button), nothing in the decomp/docs binds one (verified) -- so
    // MOUSE_RB is a reasonable default pairing with MOUSE_LB fire; revisit if a faithful
    // binding ever surfaces.
    const bool forceSkill = (m_ForceSkillFrame >= 0 && m_Frame >= m_ForceSkillFrame);
    // item 6 (H12): SK_FORCE_SKILL_PULSE -- true on EXACTLY frame K1 or frame K2,
    // never held (see the member doc comment, GameScene.hpp) -- so it produces a
    // real press-release-press-release edge sequence a toggle-type skill (C04/C08)
    // needs to prove both "activated" and "ended" headlessly.
    const bool forceSkillPulse = (m_ForceSkillPulseFrame1 >= 0 && m_Frame == m_ForceSkillPulseFrame1) ||
                                 (m_ForceSkillPulseFrame2 >= 0 && m_Frame == m_ForceSkillPulseFrame2);
    Sim::WorldInputs in;
    in.playerPos = resolved;
    in.aimDir = AimDirection();
    in.firing = Util::Input::IsKeyPressed(Util::Keycode::MOUSE_LB) &&
                m_Player->Stats().energy >= m_WeaponEnergyCost;
    in.skill = Util::Input::IsKeyDown(Util::Keycode::MOUSE_RB) || forceSkill || forceSkillPulse;
    if (forceSkill || forceSkillPulse) {
        in.firing = true; // SK_FORCE_SKILL[_PULSE] hook: hold fire so the active-skill mirror is observable.
    }
    // SK_AUTOFIRE test hook (env, NO-OP if unset): aim + fire along the autowalk
    // travel direction so a headless run can shoot through a design-box column and
    // damage the target-room enemy (corroborates box-break / clear-flow in-game).
    // SEPARATE from SK_AUTOWALK, which stays PURE NAVIGATION (no firing, no energy
    // spend) -- the clean "can the player walk the floor" semantics. The primary
    // box-break evidence is BoxDestructionTest; this hook is in-game aid only. NOT
    // game logic -- a test-input override gated on the dedicated hook.
    if (m_AutoFire && m_AutoWalk &&
        m_Player->Stats().energy >= m_WeaponEnergyCost) {
        const glm::vec2 d = m_AutoWalkTarget - resolved;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 1.0F) {
            in.aimDir = d / len;
            in.firing = true;
        }
    }
    // SK_FORCE_HIT=K test hook (env, NO-OP unless set): CONTINUOUS, not one shot --
    // from frame K onward this runs EVERY frame, re-picking the nearest alive
    // enemy/boss from the SIM's own last-frame views, re-aiming at it and holding
    // fire (subject only to the energy gate in the condition below). Wins over
    // SK_AUTOFIRE's travel-direction aim when both are set -- this hook exists
    // specifically to GUARANTEE landed hits (not just fired shots) for headless
    // proof of hit-triggered behaviour (P3.2 bullet buffs).
    if (m_ForceHitFrame >= 0 && m_Frame >= m_ForceHitFrame &&
        m_Player->Stats().energy >= m_WeaponEnergyCost) {
        glm::vec2 best{0.0F, 0.0F};
        float bestDistSq = -1.0F;
        bool found = false;
        for (const Sim::Simulation::EntityView &ev : m_Sim->EnemyViews()) {
            if (!ev.alive) {
                continue;
            }
            const glm::vec2 d = ev.pos - resolved;
            const float distSq = d.x * d.x + d.y * d.y;
            if (!found || distSq < bestDistSq) {
                best = ev.pos;
                bestDistSq = distSq;
                found = true;
            }
        }
        if (m_Sim->HasBoss()) {
            const Sim::Simulation::EntityView bv = m_Sim->BossView();
            if (bv.alive) {
                const glm::vec2 d = bv.pos - resolved;
                const float distSq = d.x * d.x + d.y * d.y;
                if (!found || distSq < bestDistSq) {
                    best = bv.pos;
                    bestDistSq = distSq;
                    found = true;
                }
            }
        }
        if (found) {
            const glm::vec2 d = best - resolved;
            const float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len > 1.0F) {
                in.aimDir = d / len;
            }
            in.firing = true;
        }
    }
    in.playerAlive = !m_Player->Stats().IsDead();
    in.playerRoomId = playerRoomId;

    m_Sim->SetPlayerStats(m_Player->Stats());      // push current vitals in
    m_Sim->Advance(dtMs, in);                       // sim damages its player-stats copy
    // SK_TRACE_BULLETS test hook (env, NO-OP if unset): log every live player bullet's sim
    // position each frame ("BulletTrace: frame id x y" in px and cells), for the P3.0
    // speed-scale measurement. Read-only; not game logic.
    static const bool traceBullets = std::getenv("SK_TRACE_BULLETS") != nullptr;
    if (traceBullets) {
        for (const Sim::BulletState &tb : m_Sim->Bullets()) {
            if (tb.active && tb.camp == 0) {
                LOG_INFO("BulletTrace: frame {} id {} px=({:.2f},{:.2f}) cells=({:.3f},{:.3f}) player=({:.2f},{:.2f})",
                         m_Frame, tb.id, tb.pos.x, tb.pos.y, tb.pos.x / kCellPx, tb.pos.y / kCellPx,
                         resolved.x, resolved.y);
            }
        }
    }
    const CombatStats &simPlayer = m_Sim->PlayerStats();
    // MARKER (decision C): pull back ONLY hp + armor. Energy is scene-owned (regen + spend
    // below); the sim never modifies energy, so do NOT copy simPlayer.energy back -- doing so
    // would clobber this frame's regen/spend with the value we pushed in. Intentional asymmetry.
    m_Player->Stats().hp = simPlayer.hp;
    m_Player->Stats().armor = simPlayer.armor;
    // P3.3.c: energy a skill restored this Advance (C07 BatBack RestoreEnergy). Energy is
    // scene-owned, so the grant is applied here like the shot spend below (T-46).
    m_Player->Stats().AddEnergy(m_Sim->PlayerEnergyGrantedLastAdvance());
    // A3 cooldown bridge: the sim wrote the skill's 0..1 charge into its player-stats
    // copy (TickSkill); pull it back so the HUD (which reads m_Player->Stats()) reaches
    // it. A4 renders the mask; A3 only makes it reachable.
    m_Player->Stats().skillCdProgress = simPlayer.skillCdProgress;
    for (int s = 0; s < m_Sim->PlayerShotsLastAdvance(); ++s) {
        m_Player->Stats().SpendEnergy(m_WeaponEnergyCost);
    }
    if (m_ForceSkillFrame >= 0 && (m_Frame % 30) == 0) { // SK_FORCE_SKILL: periodic skill trace.
        const bool inSkill = m_Sim->PlayerSkill() && m_Sim->PlayerSkill()->InSkill();
        const bool ready = m_Sim->PlayerSkill() && m_Sim->PlayerSkill()->SkillReady();
        LOG_INFO("SKILL frame={} progress={:.2f} inSkill={} ready={} shots={}", m_Frame,
                 m_Player->Stats().skillCdProgress, inSkill, ready,
                 m_Sim->PlayerShotsLastAdvance());
    }
    // P3.3.a headless hook: "Skill: <char> ended" trace, edge-detected on InSkill()
    // true->false (mirrors "skill_use" activation trace above + the "Buff: ... ended"
    // convention, P3.1). NO-OP on behaviour -- logging/bookkeeping only.
    if (m_Sim->PlayerSkill() != nullptr) {
        const bool inSkillNow = m_Sim->PlayerSkill()->InSkill();
        if (m_SkillWasActive && !inSkillNow) {
            LOG_INFO("Skill: {} ended", m_CharId);
        }
        m_SkillWasActive = inSkillNow;
    }

    // --- A: present the sim's anim/sfx cues (muzzle/hit/death). Drained before the view
    // sync below so a dying entity's view is still alive to anchor its death effect. ---
    ConsumeSimEvents(resolved);

    // --- Clear-room gating: RGRoomX is the SINGLE authority (#3a) ---
    // Enter combat: the player is in an Uncleared room that still has hostiles ->
    // StartRoom (Active + doors close, door_open=0). Clear: an Active room whose
    // hostiles are all dead -> ClearRoom (Cleared + reward gate room_type==1 +
    // doors open, door_open=1). The old per-frame m_LockedRoom derivation is GONE;
    // BlocksAny seals doors purely from RGRoomX.door_open. A room never re-locks
    // once Cleared (the player can't be trapped in a cleared room).
    //
    // ARM TIMING (air-wall fix): the seal must arm only once the player's body is
    // PAST the door-seal band, never while it still overlaps the entry door cell.
    // playerRoomId stays a plain ContainsPoint test (above) so enemy WAKE timing
    // -- driven by in.playerRoomId into the sim -- is byte-unchanged (golden/combat
    // hash safe); only this LOCK gate is inset (kCellPx + body radius + 1) px deep. Entering
    // the rect (wake) and committing deep enough to lock are now two thresholds.
    if (playerRoomId >= 0 &&
        m_RoomLife[static_cast<std::size_t>(playerRoomId)].State() ==
            RGRoomX::Process::Uncleared &&
        m_Rooms[static_cast<std::size_t>(playerRoomId)]->ContainsPointInset(
            resolved, kCellPx + m_PlayerRadiusPx + 1.0F) &&
        RoomHasLiveHostile(playerRoomId)) {
        m_RoomLife[static_cast<std::size_t>(playerRoomId)].StartRoom();
        LOG_INFO("RGRoomX: room {} StartRoom -> Active (door_open=0, sealed)",
                 playerRoomId);
        // FAITHFUL: RGMusicManager effect_list[4] == fx_door.wav (door seal).
        m_Audio->PlaySfx(m_Data.FindEffectClip(4));
        // T-29: the show-up cue is NOT played here any more. FAITHFUL @
        // EnemyMaker$$CreatePart (game_typed.c:78030-78082): the cue is
        // `if (part != 0) Invoke("ShowUpEffect", 0.8f)` -- i.e. it fires 0.8 s
        // after the 2nd and later spawn PARTS of a wave, not on room-lock, and
        // EnemyMaker__ShowUpEffect (game_typed.c:78114-78126) is exactly
        // RGMusicManager__PlayEffect(this, 7). It is wired at the real trigger in
        // SpawnWaveBatch() via WaveScheduler::kShowUpEffectDelayMs (0.8 s ==
        // 0x3f4ccccd, the Invoke's float literal) and fired by
        // TickShowUpCues(dtMs) below.
        // INFERRED (T-34): switch to the biome's "high"/combat BGM variant, when the
        // source ships one, on room-lock; back to "low" on clear (below). No
        // decompiled trigger was found for this -- see AudioTable's doc comment.
        if (!m_BgmHigh) {
            m_BgmHigh = true;
            m_Audio->PlayBgm(m_Data.BgmFor(LevelBiome(), true));
        }
    }
    TickShowUpCues(dtMs); // T-29: EnemyMaker's 0.8 s Invoke("ShowUpEffect") timers.
    for (std::size_t i = 0; i < m_RoomLife.size(); ++i) {
        // MAP M1: a room with parts still queued is NOT clear -- the next part
        // spawns on the first tick the room has no live hostile, no delay
        // (FAITHFUL: EnemyMaker.EnemyDead -> CreatePart() immediately,
        // game_typed.c:73138-73185; the 0.8 s Invoke in CreatePart is a
        // show-up SOUND cue for later parts, not a spawn gate -- it is now wired
        // at the spawn site, see WaveScheduler::kShowUpEffectDelayMs and
        // SpawnWaveBatch/TickShowUpCues, T-29).
        if (m_RoomLife[i].State() == RGRoomX::Process::Active &&
            !RoomHasLiveHostile(static_cast<int>(i)) && i < m_Waves.size() &&
            !m_Waves[i].wave.pending.empty()) {
            SpawnWaves &wv = m_Waves[i];
            SpawnWaveBatch(static_cast<int>(i), wv.wave.Tick(dtMs, /*roomEmpty=*/true));
            continue;
        }
        if (m_RoomLife[i].State() == RGRoomX::Process::Active &&
            !RoomHasLiveHostile(static_cast<int>(i))) {
            m_RoomLife[i].ClearRoom(); // process=Cleared, reward gate, OpenDoor
            // FAITHFUL: RGMusicManager effect_list[4] == fx_door.wav (door reopens).
            m_Audio->PlaySfx(m_Data.FindEffectClip(4));
            // INFERRED (T-34): back to the "low"/ambient BGM loop once the room the
            // player is standing in is the one that just cleared -- see the StartRoom
            // switch above.
            if (m_BgmHigh && static_cast<int>(i) == playerRoomId) {
                m_BgmHigh = false;
                m_Audio->PlayBgm(m_Data.BgmFor(LevelBiome(), false));
            }
            // NOT PORTED T-10: GetRoomReward (clear-room reward) is not resolved; log only.
            if (m_RoomLife[i].RewardGranted()) {
                // reward gate FIRED (room_type==1). Gate lands here; the actual
                // reward CONTENT (loot overlay) is the unverified special-room debt.
                LOG_INFO("RGRoomX: room {} cleared -> reward gate fired (room_type==1)",
                         i);
            }
        }
    }

    // M3d: door bars follow RGRoomX.door_open (RGDoor.CloseDoor / OpenDoor).
    for (DoorPiece &p : m_DoorPieces) {
        const bool sealed = p.room >= 0 && static_cast<std::size_t>(p.room) < m_RoomLife.size() &&
                            !m_RoomLife[static_cast<std::size_t>(p.room)].DoorOpen();
        if (sealed != p.shown) {
            p.shown = sealed;
            const float sc = sealed ? Scale::kSpriteScale : 0.0F;
            p.bar->m_Transform.scale = {sc, sc};
            p.shadow->m_Transform.scale = {sc, sc};
        }
    }

    // SK_AUTOWALK: log each room transition (start -> -1 corridor -> neighbour) so
    // the headless run shows real room->corridor->room traversal, that the corridor
    // is neutral (locked=-1 while playerRoom=-1), and that the spawn is not wedged.
    if (m_AutoWalk && playerRoomId != m_LastRoomId) {
        const bool locked =
            playerRoomId >= 0 &&
            !m_RoomLife[static_cast<std::size_t>(playerRoomId)].DoorOpen();
        LOG_INFO("AUTOWALK frame={} room {} -> {} pos=({:.0f},{:.0f}) locked={}",
                 m_Frame, m_LastRoomId, playerRoomId, resolved.x, resolved.y, locked);
        m_LastRoomId = playerRoomId;
    }

    // --- Sync enemy/boss render views from the sim (entities are never erased: null on death) ---
    const std::vector<Sim::Simulation::EntityView> eviews = m_Sim->EnemyViews();
    for (const Sim::Simulation::EntityView &ev : eviews) {
        if (ev.id >= m_Enemies.size()) {
            continue;
        }
        std::shared_ptr<Enemy> &view = m_Enemies[ev.id];
        if (view == nullptr) {
            continue;
        }
        view->m_Transform.translation = ev.pos;
        view->SyncSprite(); // scale + this frame's pivot (feet anchored)
        if (!ev.alive) {
            m_Renderer.RemoveChild(view);
            view = nullptr;
        }
    }
    if (!m_Bosses.empty() && m_Bosses[0] != nullptr && m_Sim->HasBoss()) {
        const Sim::Simulation::EntityView bv = m_Sim->BossView();
        m_Bosses[0]->m_Transform.translation = bv.pos;
        m_Bosses[0]->SyncSprite();
        if (!bv.alive) {
            m_Renderer.RemoveChild(m_Bosses[0]);
            m_Bosses[0] = nullptr;
            LOG_INFO("Boss defeated!");
            // M3b: RGChestBoss -- the boss's chest (chest_boss<NN>: tier + its
            // signature weapon) drops where the boss fell.
            {
                const std::string root(RESOURCE_DIR);
                const BossChestDef *bc =
                    m_BossDef != nullptr ? m_Data.FindBossChest(m_BossDef->id) : nullptr;
                // The chest drops where the boss died; the gate opens at the room's
                // b_point (centre). A boss that dies on the centre would bury its
                // chest under the portal -- PORT CHOICE T-07: nudge the chest two cells
                // below the gate when the two would overlap.
                glm::vec2 chestPos = bv.pos;
                if (glm::distance(chestPos, m_GatePos) < 2.0F * kCellPx) {
                    chestPos = m_GatePos + glm::vec2(0.0F, -2.0F * kCellPx);
                }
                auto chest = std::make_shared<Chest>(root, chestPos, bc != nullptr ? bc->level : 2,
                                                     Chest::Kind::Boss);
                if (bc != nullptr) {
                    chest->SetGuaranteed(bc->weapons);
                }
                m_Chests.push_back(chest);
                m_Renderer.AddChild(chest);
                LOG_INFO("Chest: boss chest {} (tier {}, {} signature weapon(s))",
                         bc != nullptr ? bc->id : std::string("chest_boss?"), chest->Tier(),
                         bc != nullptr ? bc->weapons.size() : 0u);
            }
        }
    }

    // --- Sync bullet views ---
    SyncBulletViews();

    // --- Player death (step 2b): no longer an app quit -- signal the controller to
    // Replace this floor with a minimal EndScene. Captured-while-alive + deferred,
    // same timing as a floor clear. The death check runs BEFORE the clear check, so a
    // dead player never triggers a floor transition. m_Transitioning is shared so we
    // signal exactly once (death takes precedence). ---
    if (m_Player->Stats().IsDead()) {
        if (m_Run != nullptr) {
            if (!m_Transitioning) {
                m_Transitioning = true;
                {
                    // DeathTrace (P2.18 H3 A/B diagnostics, no behaviour): when, in simulated time, and
                    // against how many live hostiles the hero died.
                    int alive = 0;
                    for (const Sim::Simulation::EntityView &ev : m_Sim->EnemyViews()) {
                        alive += ev.alive ? 1 : 0;
                    }
                    LOG_INFO("DeathTrace: frame {} rawMs {:.0f} simMs {:.0f} meanFrameMs {:.2f} enemiesAlive {} bossAlive {}",
                             m_Frame, m_RawMsTotal, m_SimMsTotal,
                             m_Frame > 0 ? m_RawMsTotal / static_cast<float>(m_Frame) : 0.0F, alive,
                             (m_Sim->HasBoss() && m_Sim->BossView().alive) ? 1 : 0);
                }
                LOG_INFO("Player died on floor {} -> EndScene", m_FloorIndex);
                m_Run->OnPlayerDied();
            }
        } else {
            // No controller (defensive fallback): keep the old quit behaviour.
            Core::Context::GetInstance()->SetExit(true);
        }
        return;
    }

    // --- Chests: open on proximity, roll loot, drop a weapon pickup ---
    const glm::vec2 playerPos = resolved;
    for (auto &chest : m_Chests) {
        if (chest->Opened() ||
            glm::distance(playerPos, chest->Position()) > kChestOpenRange) {
            continue;
        }
        chest->Open();
        // FAITHFUL (T-35): audio.json ui_effects.chest_open, generated from
        // res/item/chest01.prefab RGChest.audio_clip -- a real rgsound/effect
        // clip that is NOT in RGMusicManager's 11-entry effect_list.
        m_Audio->PlaySfx(UiClip("chest_open"));
        // RGChestBoss: CreateBossWeapon (object_list, guaranteed) + CreateSampleWeapon
        // (a roll); RGChest / RGChestRoomReward: the tier roll (GetWeapon).
        std::vector<const WeaponDef *> drops;
        for (const std::string &wid : chest->Guaranteed()) {
            if (const WeaponDef *g = m_Data.FindWeapon(wid)) {
                drops.push_back(g);
            }
        }
        const std::string dropId = m_Loot.Roll(chest->Tier(), m_Rng);
        if (const WeaponDef *def = m_Data.ResolveDropWeapon(dropId)) {
            drops.push_back(def);
        }
        for (std::size_t di = 0; di < drops.size(); ++di) {
            const float dx = (static_cast<float>(di) - 0.5F * static_cast<float>(drops.size() - 1)) * kCellPx;
            auto pickup = std::make_shared<WeaponPickup>(
                std::string(RESOURCE_DIR), FreeDropPoint(chest->Position() + glm::vec2(dx, -kCellPx)), drops[di]);
            m_Pickups.push_back(pickup);
            m_Renderer.AddChild(pickup);
            LOG_INFO("Chest: opened -> {}", drops[di]->id);
        }
    }

    // --- Pickups: walk over one to equip it (rebuilds the sim WeaponController) ---
    for (auto it = m_Pickups.begin(); it != m_Pickups.end();) {
        if (glm::distance(playerPos, (*it)->Position()) <= kPickupRange) {
            const WeaponDef *def = (*it)->Def();
            ++m_WeaponSwaps;
            m_Sim->EquipWeapon(*def, def->id, m_FloorSeed + 5 + m_WeaponSwaps, &m_Data); // P3.2
            m_WeaponEnergyCost = WeaponEnergyCost(*def);
            m_CurrentWeaponId = def->id; // keep the weapon-id carry vehicle in sync on every equip
            m_Renderer.RemoveChild(*it);
            it = m_Pickups.erase(it);
        } else {
            ++it;
        }
    }

    // --- B3: coins / energy orbs scatter, then home on the player and are collected
    //     (RGCoin.FixedUpdate + GetItem: coin_value += value / RGEnergy: energy refill). ---
    for (auto it = m_Coins.begin(); it != m_Coins.end();) {
        if ((*it)->Update(dtMs, playerPos)) {
            if ((*it)->Kind() == RewardKind::Energy) {
                CombatStats &st = m_Player->Stats();
                st.energy = std::min(st.maxEnergy, st.energy + (*it)->Value());
            } else if (m_Run != nullptr) {
                m_Run->AddCoins((*it)->Value());
            }
            LOG_INFO("Pickup: {} +{} (coins now {})",
                     (*it)->Kind() == RewardKind::Energy ? "energy" : "coin", (*it)->Value(),
                     m_Run != nullptr ? m_Run->State().stats.coins : 0);
            // FAITHFUL: RGMusicManager effect_list[1]/[2] == fx_coin.wav / fx_energy.wav.
            m_Audio->PlaySfx(m_Data.FindEffectClip((*it)->Kind() == RewardKind::Energy ? 2 : 1));
            m_Renderer.RemoveChild(*it);
            it = m_Coins.erase(it);
        } else {
            ++it;
        }
    }

    // --- M3c: shop machines -- stand in the 2x2 trigger and press the use key
    //     (E; the original's item button) to buy. SK_SHOP_BUY=K teleports to the
    //     first machine and buys on frame K (headless proof). ---
    {
        const bool useKey = Util::Input::IsKeyDown(Util::Keycode::E);
        const bool hookBuy = m_ShopBuyFrame >= 0 && m_Frame == m_ShopBuyFrame && !m_Machines.empty();
        if (hookBuy) {
            const char *mi = std::getenv("SK_SHOP_MACHINE"); // which machine (default 0)
            const std::size_t k = std::min(m_Machines.size() - 1,
                                           static_cast<std::size_t>(mi != nullptr ? std::atol(mi) : 0));
            m_Player->m_Transform.translation = m_Machines[k]->Position() + glm::vec2(0.0F, -40.0F);
        }
        if (useKey || hookBuy) {
            const glm::vec2 pp = m_Player->Position();
            for (auto &m : m_Machines) {
                if (!m->Sold() && glm::distance(pp, m->Position()) <= 2.0F * kCellPx) {
                    Purchase(*m);
                    break;
                }
            }
        }
    }

    // --- M3e: worship the statue (TalkStatue.ItemTrigger: pay item_value, set
    //     RGGameProcess.statue_index). E in range, or SK_STATUE_WORSHIP=K. ---
    if (m_Statue.index > 0 && !m_Statue.used && m_Run != nullptr) {
        const bool hook = m_StatueWorshipFrame >= 0 && m_Frame == m_StatueWorshipFrame;
        if (hook) {
            m_Player->m_Transform.translation = m_Statue.pos + glm::vec2(0.0F, -40.0F);
        }
        if ((hook || Util::Input::IsKeyDown(Util::Keycode::E)) &&
            glm::distance(m_Player->Position(), m_Statue.pos) <= 2.0F * kCellPx) {
            if (m_Run->SpendCoins(m_Statue.cost)) {
                m_Statue.used = true;
                m_Run->SetStatue(m_Statue.index);
                if (m_Statue.tag) {
                    m_Statue.tag->SetText(std::string("BLESSED: ") + kStatueName[m_Statue.index]);
                }
                LOG_INFO("Statue: worshipped statue_{:02d} ({}) -> statue_index {}", m_Statue.index,
                         kStatueName[m_Statue.index], m_Statue.index);
            } else {
                LOG_INFO("Statue: not enough money ({} < {})", m_Run->State().stats.coins, m_Statue.cost);
            }
        }
    }
    if (m_StatueCdMs > 0.0F) {
        m_StatueCdMs -= dtMs;
    }

    // --- A: age out transient presentation effects (they self-animate on wall-clock) ---
    for (auto it = m_Effects.begin(); it != m_Effects.end();) {
        it->second -= dtMs;
        if (it->second <= 0.0F) {
            m_Renderer.RemoveChild(it->first);
            it = m_Effects.erase(it);
        } else {
            ++it;
        }
    }

    // --- Camera follow ---
    m_Camera.Follow(m_Player->Position(), 0.1F);
    m_Camera.Update(dtMs);

    // --- Forward run loop (step 2a): whole-floor clear -> snapshot -> transition ---
    // This is the END of Update on purpose: the snapshot must reflect this frame's
    // FINAL hp/armor/energy (after the sim pull-back above) and the FINAL weapon +
    // m_CurrentWeaponId (after the pickup->equip block above). The clear test reads
    // the sim's AUTHORITATIVE liveness (FloorCleared -> EnemyViews/BossView), not the
    // render mirrors. CAPTURE-BEFORE-DESTROY: we build the snapshot here, while
    // m_Player is alive, and hand it to OnFloorCleared, which copies it into RunState
    // and requests a DEFERRED Replace -- the SceneManager tears this scene (and
    // m_Player) down only after this Update returns, so the snapshot is safe.
    const bool forceClear = (m_ForceClearFrame >= 0 && m_Frame >= m_ForceClearFrame);
    // MAP M2: FAITHFUL floor exit -- the player walks into the transfer gate
    // (RGTransferGate.ItemTrigger -> RGGameProcess.NextScene). The gate opens once
    // its room has no live hostile and no pending part (boss floors: the boss is
    // dead). Drops left behind are left behind, as in the original. The old
    // auto-advance-on-clear is gone; SK_FORCE_CLEAR still forces the exit.
    if (!m_GateActive && m_GateRoom >= 0 && !RoomHasLiveHostile(m_GateRoom) &&
        (static_cast<std::size_t>(m_GateRoom) >= m_Waves.size() ||
         m_Waves[static_cast<std::size_t>(m_GateRoom)].wave.pending.empty()) &&
        (!IsBossFloor(m_FloorIndex) || m_HadHostiles)) {
        const std::string root(RESOURCE_DIR);
        m_GateActive = true;
        m_GateLight = std::make_shared<Util::GameObject>();
        auto lightImg = std::make_shared<Util::Image>(root + "/sprites/texiao_01.png");
        m_GateLight->SetDrawable(lightImg);
        m_GateLight->m_Transform.translation = m_GatePos;
        {   // light child: 2 x 3 world units (1 unit = kCellPx) regardless of atlas size
            const glm::vec2 sz = lightImg->GetSize();
            m_GateLight->m_Transform.scale = {2.0F * kCellPx / std::max(1.0F, sz.x),
                                              3.0F * kCellPx / std::max(1.0F, sz.y)};
        }
        m_GateLight->SetZIndex(RenderLayout::kZGateLight);
        m_Renderer.AddChild(m_GateLight);
        m_Gate = std::make_shared<Util::GameObject>();
        // FAITHFUL @ transfer_gate_1.controller: effect04_5..12, 8 fps loop; img 2.5x3.5.
        std::vector<std::string> gateFrames;
        for (int i = 5; i <= 12; ++i) {
            gateFrames.push_back(root + "/sprites/effect04_" + std::to_string(i) + ".png");
        }
        m_Gate->SetDrawable(std::make_shared<Util::Animation>(gateFrames, true, 125, true));
        m_Gate->m_Transform.translation = m_GatePos;
        {   // img child: 2.5 x 3.5 world units (transfer_gate.prefab localScale)
            const glm::vec2 sz = Util::Image(gateFrames.front()).GetSize();
            m_Gate->m_Transform.scale = {2.5F * kCellPx / std::max(1.0F, sz.x),
                                         3.5F * kCellPx / std::max(1.0F, sz.y)};
        }
        m_Gate->SetZIndex(RenderLayout::kZGate);
        m_Renderer.AddChild(m_Gate);
        LOG_INFO("TransferGate: open in room {} at ({:.0f},{:.0f})", m_GateRoom, m_GatePos.x,
                 m_GatePos.y);
    }
    const bool gateEntered =
        m_GateActive && glm::distance(playerPos, m_GatePos) <= kGateTriggerPx;
    if (gateEntered && !m_Transitioning) {
        LOG_INFO("TransferGate: entered -> next floor");
        // PORT CHOICE: RGMusicManager effect_list[6] == fx_switch.wav; the closest
        // "activation" cue in the 11-clip table to a gate/lever trigger.
        m_Audio->PlaySfx(m_Data.FindEffectClip(6));
    }
    if (!m_Transitioning && m_Run != nullptr && (gateEntered || forceClear)) {
        m_Transitioning = true; // signal exactly once
        if (forceClear) {
            // Test hook only: bleed 1 hp so cross-floor hp-continuation is observable
            // headlessly (no combat/input). Never runs in normal play (env unset).
            CombatStats &st = m_Player->Stats();
            st.hp = std::max(1, st.hp - 1);
        }
        // B5: carry the hero too -- without it the snapshot's charId defaulted to
        // "c01" and a picked hero reverted to Knight on the very next floor.
        const PlayerContinuation snapshot{m_Player->Stats(), m_CurrentWeaponId, m_CharId};
        LOG_INFO("Floor {} CLEARED -> snapshot hp={} armor={} energy={} weapon='{}' (next floor {})",
                 m_FloorIndex, snapshot.stats.hp, snapshot.stats.armor,
                 snapshot.stats.energy, snapshot.weaponId, m_FloorIndex + 1);
        m_Run->OnFloorCleared(snapshot); // carry + ++floorIndex + deferred Replace
    }
}

void GameScene::SpawnEnemy(const std::string &id, glm::vec2 at, int roomIndex, int serial) {
    const std::string root(RESOURCE_DIR);
    const EnemyDef *rdef = m_Data.FindEnemy(id);
    if (rdef == nullptr) {
        rdef = m_Data.FindEnemyByClass("EnemyAI01"); // unknown species id: representative
    }
    if (rdef == nullptr) {
        return;
    }
    // P2.18 (RC1, PORT CHOICE T-63): seat the body where ITS radius is free of every blocker --
    // the requested spawn point if it fits, else the nearest of the room's spawn points that does,
    // else a depenetrated position. Without this an enemy spawned inside a box stayed pinned.
    {
        const float bodyR = Sim::MakeBodyGeom(rdef->body, rdef->hurtbox, Sim::kEnemyBodyRadius).bodyRadiusPx;
        const std::vector<glm::vec2> noPoints;
        const std::vector<glm::vec2> &pts = (roomIndex >= 0 && static_cast<std::size_t>(roomIndex) < m_Waves.size())
                                                ? m_Waves[static_cast<std::size_t>(roomIndex)].points
                                                : noPoints;
        const glm::vec2 want = at;
        const std::size_t roomU = roomIndex >= 0 ? static_cast<std::size_t>(roomIndex) : m_RoomDoors.size();
        at = ResolveSpawnPoint(pts, at, bodyR, [this, roomU](glm::vec2 p, float r) { return SpawnBlocked(roomU, p, r); });
        at = Depenetrate(at, bodyR);
        if (at != want) {
            LOG_INFO("Spawn: {} room {} moved ({:.1f},{:.1f}) -> ({:.1f},{:.1f}) (body r={:.1f} did not fit)",
                     rdef->id, roomIndex, want.x, want.y, at.x, at.y, bodyR);
        }
    }
    const int seed = m_FloorSeed + 1000 + roomIndex + serial * 131;
    auto enemy = std::make_shared<Enemy>(*rdef, root, at, 450.0F, 260.0F,
                                         m_Data.SpriteFrames(rdef->id, root),
                                         m_Data.SpriteIntervalMs(rdef->id, 120));
    enemy->SetAnchor(MakeSpriteAnchor(m_Data, rdef->id, rdef->body, rdef->hurtbox, rdef->bodyScale,
                                      {rdef->spriteOffsetX, rdef->spriteOffsetY},
                                      Sim::kEnemyBodyRadius));
    enemy->AI().SetSeed(seed);
    enemy->AI().SetKinematic(rdef->kinematic != 0);
    enemy->SetRoomId(roomIndex);
    m_Enemies.push_back(enemy);
    m_EnemyDefs.push_back(rdef);
    m_Renderer.AddChild(enemy);
    m_Sim->AddEnemy(*rdef, at, roomIndex, seed);
    m_HadHostiles = true;
}

void GameScene::SpawnNextWave(int roomIndex) {
    if (roomIndex < 0 || static_cast<std::size_t>(roomIndex) >= m_Waves.size()) {
        return;
    }
    SpawnWaves &wv = m_Waves[static_cast<std::size_t>(roomIndex)];
    if (wv.points.empty()) {
        wv.wave.pending.clear();
        return;
    }
    SpawnWaveBatch(roomIndex, wv.wave.TakeWave());
}

// AUDIO (T-35): audio.json "ui_effects" lookup for the clips the SCENE plays
// directly (chest open, hurt/death), each with its provenance recorded in the
// data rather than as a bare literal here.
//
// DOCUMENTED EXCEPTION -- kAudioTableMissingFallback below is the ONLY clip
// filename literal left in C++. It is used solely when the audio TABLE lookup
// itself fails: audio.json absent (a checkout without the sibling _reverse dump)
// or a ui_effects key the generator could not resolve. In that situation
// Resources/audio/ is normally absent too, so AudioPlayer no-ops on it anyway;
// it exists so a missing table degrades to one known-shipped generic hit cue
// (RGMusicManager effect_list[5]) instead of to an unnamed clip.
const std::string &GameScene::UiClip(const char *key) const {
    static const std::string kAudioTableMissingFallback = "fx_hit.wav";
    const std::string &clip = m_Data.UiEffectClip(key);
    return clip.empty() ? kAudioTableMissingFallback : clip;
}

void GameScene::SpawnWaveBatch(int roomIndex, const std::vector<std::string> &batch) {
    if (batch.empty() || roomIndex < 0 ||
        static_cast<std::size_t>(roomIndex) >= m_Waves.size()) {
        return;
    }
    SpawnWaves &wv = m_Waves[static_cast<std::size_t>(roomIndex)];
    if (wv.points.empty()) {
        return;
    }
    for (const std::string &id : batch) {
        // Spread this part over the room's spawn points (serial-indexed stride).
        // PORT CHOICE (T-04): point spread is the port's stride; the original's
        // spawn placement is in the truncated CreateEnemy.
        const std::size_t pi =
            static_cast<std::size_t>(wv.serial * 7 + 3) % wv.points.size();
        SpawnEnemy(id, wv.points[pi], roomIndex, wv.serial);
        ++wv.serial;
    }
    // T-29: FAITHFUL @ EnemyMaker$$CreatePart (game_typed.c:78030-78082). After
    // spawning a part, the original does `if (part != 0) Invoke("ShowUpEffect",
    // 0.8f); part = part + 1;` -- so the cue fires for every part EXCEPT the
    // first, 0.8 s after that part spawns. CreatePart is EnemyMaker-wide in the
    // decompile (not boss-only), so every multi-part wave gets it, boss room
    // included. ShowUpEffect == RGMusicManager.PlayEffect(7) == effect_list[7]
    // (game_typed.c:78114-78126).
    if (wv.partsSpawned != 0) {
        m_ShowUpCuesMs.push_back(Game::WaveScheduler::kShowUpEffectDelayMs);
    }
    ++wv.partsSpawned;
    LOG_INFO("EnemyMaker: room {} part spawned {} ({} pending)", roomIndex, batch.size(),
             wv.wave.pending.size());
}

// T-29: drain the pending Invoke("ShowUpEffect", 0.8f) timers scheduled by
// SpawnWaveBatch(). A plain countdown list rather than a general scheduler: this
// is the only delayed-callback the scene needs, and keeping it local keeps the
// sim's own FixedClock/Scheduler (which owns combat determinism) untouched.
void GameScene::TickShowUpCues(float dtMs) {
    if (m_ShowUpCuesMs.empty()) {
        return;
    }
    std::size_t w = 0;
    for (std::size_t r = 0; r < m_ShowUpCuesMs.size(); ++r) {
        const float left = m_ShowUpCuesMs[r] - dtMs;
        if (left <= 0.0F) {
            m_Audio->PlaySfx(m_Data.FindEffectClip(7)); // effect_list[7] == fx_show_up.wav
        } else {
            m_ShowUpCuesMs[w++] = left;
        }
    }
    m_ShowUpCuesMs.resize(w);
}

// AUDIO (P6.7): this floor's biome key for audio.json bgm.by_biome. "1-forest"
// when the level table is absent -- that is map_A1's biome, the same floor the
// pre-M1 fallback tileset draws, and its bgm_clip is bgm_1Low.ogg (also the
// Title screen's clip), so a table-less checkout still gets the chapter-1 loop.
const std::string &GameScene::LevelBiome() const {
    static const std::string kFallbackBiome = "1-forest";
    return (m_Level != nullptr && !m_Level->biome.empty()) ? m_Level->biome : kFallbackBiome;
}

bool GameScene::PendingWaves() const {
    for (const SpawnWaves &w : m_Waves) {
        if (!w.wave.pending.empty()) {
            return true;
        }
    }
    return false;
}

void GameScene::Purchase(VendingMachine &m) {
    if (m_Run == nullptr) {
        return;
    }
    // FAITHFUL: ObjectVendingMachine.ItemTrigger -- pay item_value from
    // RGGameProcess.coin_value; I_no_money ("Not enough money") otherwise.
    if (!m_Run->SpendCoins(m.Price())) {
        LOG_INFO("Shop: not enough money ({} < {})", m_Run->State().stats.coins, m.Price());
        return;
    }
    CombatStats &st = m_Player->Stats();
    switch (m.GetKind()) {
    case VendingMachine::Kind::Item: {
        if (const PotDef *pd = m_Data.FindPot(m.Stock())) {
            // RGHealthPot / RGEnergyPot / RGBothPot.ItemTrigger: restore, clamped to max.
            st.hp = std::min(st.maxHp, st.hp + pd->hp);
            st.energy = std::min(st.maxEnergy, st.energy + pd->energy);
            LOG_INFO("Shop: bought {} for {} (+{} hp, +{} energy) -> hp {} energy {}", pd->id, m.Price(),
                     pd->hp, pd->energy, st.hp, st.energy);
        }
        break;
    }
    case VendingMachine::Kind::Weapon: {
        if (const WeaponDef *wd = m_Data.FindWeapon(m.Stock())) {
            auto pickup = std::make_shared<WeaponPickup>(
                std::string(RESOURCE_DIR), FreeDropPoint(m.Position() + glm::vec2(0.0F, -1.5F * kCellPx)), wd);
            m_Pickups.push_back(pickup);
            m_Renderer.AddChild(pickup);
            LOG_INFO("Shop: bought weapon {} for {}", wd->id, m.Price());
        }
        break;
    }
    case VendingMachine::Kind::Water:
        // ItemWaterDispenser.ItemTrigger body did not survive (a Talk coroutine);
        // PORT CHOICE T-12: a free full energy refill, once.
        st.energy = st.maxEnergy;
        LOG_INFO("Shop: water dispenser -> energy {}", st.energy);
        break;
    }
    m.MarkSold();
}

void GameScene::SpawnReward(bool isBoss, std::uint32_t entityId, glm::vec2 pos) {
    const EnemyDef *def = nullptr;
    if (isBoss) {
        def = m_BossDef;
    } else if (entityId < m_EnemyDefs.size()) {
        def = m_EnemyDefs[entityId];
    }
    if (def == nullptr) {
        return;
    }
    // INFERRED (T-18): RGEController.GetReward -- the decompilation shows only
    // rg_random.Range(0,100) before the singleton call truncates it; the
    // `< reward_rate` gate and the reward_value[4] = [gold, silver, copper, energy]
    // count layout are inferred (PrefabManager.GetCoin 0..2 clamp, PrefabName.energy).
    const std::vector<RewardDrop> drops = RollEnemyReward(*def, m_RewardRng, RewardValuesFrom(m_Data));
    if (drops.empty()) {
        return;
    }
    const std::string root(RESOURCE_DIR);
    int coinsTotal = 0;
    int energyTotal = 0;
    for (const RewardDrop &d : drops) {
        // Scatter direction: a deterministic angle per drop (RGCoin.GetForce).
        const float ang = m_RewardRng.Range(0.0F, 6.2831853F);
        const PickupDef *pd = m_Data.FindPickup(RewardKindName(d.kind));
        if (pd == nullptr) {
            continue; // pickups.json absent: no drop entity (logged at load)
        }
        auto coin = std::make_shared<CoinPickup>(root, pos, d.kind,
                                                 glm::vec2{std::cos(ang), std::sin(ang)}, *pd,
                                                 m_Data.CoinFriction(), kScatterImpulseUnits);
        m_Coins.push_back(coin);
        m_Renderer.AddChild(coin);
        (d.kind == RewardKind::Energy ? energyTotal : coinsTotal) += d.value;
    }
    LOG_INFO("Reward: {} dropped {} pickups (coins {} energy {})", def->id, drops.size(),
             coinsTotal, energyTotal);
}

void GameScene::ConsumeSimEvents(glm::vec2 playerPos) {
    const std::string root(RESOURCE_DIR);
    const std::vector<Sim::Simulation::EntityView> evs = m_Sim->EnemyViews();
    for (const Sim::SimEvent &e : m_Sim->DrainEvents()) {
        if (e.type != Sim::SimEventType::AnimTrigger) {
            continue; // the sim emits no Sfx-typed events yet; audio rides on the names below.
        }
        // Resolve the cue's world anchor from its entity id.
        glm::vec2 pos = playerPos;
        const bool isPlayer = (e.entityId == Sim::Simulation::kPlayerViewId);
        const bool isBoss = (e.entityId == Sim::Simulation::kBossViewId);
        if (isBoss) {
            if (m_Sim->HasBoss()) {
                pos = m_Sim->BossView().pos;
            }
        } else if (!isPlayer) {
            if (e.entityId < evs.size()) {
                pos = evs[e.entityId].pos;
            }
        }

        // P3.1: buff lifecycle cues carry their payload in the name string (no
        // extra SimEvent fields needed): "buff_applied:<Kind>:<durationSec>" /
        // "buff_ended:<Kind>". Presentation-free for now (P3.2 adds VFX); just log.
        if (e.name.rfind("buff_applied:", 0) == 0) {
            const std::string rest = e.name.substr(std::strlen("buff_applied:"));
            const std::size_t sep = rest.find(':');
            const std::string kind = (sep == std::string::npos) ? rest : rest.substr(0, sep);
            const std::string sec = (sep == std::string::npos) ? "?" : rest.substr(sep + 1);
            LOG_INFO("Buff: {} applied to {} for {}s", kind, e.entityId, sec);
            continue;
        }
        if (e.name.rfind("buff_ended:", 0) == 0) {
            const std::string kind = e.name.substr(std::strlen("buff_ended:"));
            LOG_INFO("Buff: {} ended on {}", kind, e.entityId);
            continue;
        }

        if (e.name == "fire" || e.name == "attack") {
            SpawnEffect({root + "/sprites/effect01.png"}, pos, 80.0F); // muzzle flash
            // AUDIO: "fire" is player-only (kPlayerViewId, see Simulation.cpp) so this
            // is always the player's own shot -- the equipped weapon's REAL audio_clip
            // (audio.json weapon_shoot, guid-resolved from RGWeapon.audio_clip).
            // "attack" (enemy/boss) is NOT wired: enemies.json carries no gun/audio_clip
            // link (EnemyDef has no gun-id field; the 10-row enemy_gun_shoot table only
            // covers ex_orc05/EGun001), and the boss atk .wav filenames (fx_boss<N>_atk*)
            // don't consistently match the boss prefab numbering (e.g. boss09 keeps a
            // leading zero, boss01 doesn't) -- guessing one would ship an unverified
            // clip labelled as faithful, so it's left silent.
            if (isPlayer) {
                m_Audio->PlaySfx(m_Data.WeaponShootClip(m_CurrentWeaponId));
            }
        } else if (e.name == "hurt") {
            SpawnEffect(EffectFrames(root, "effect03", 3), pos, 180.0F); // hit spark
            // AUDIO: prefer the data clip (RGMusicManager effect_list[5] == fx_hit.wav,
            // the FAITHFUL generic PlayEffect(5) hit cue) when audio.json is loaded;
            // else the ui_effects fallback rows. PORT CHOICE (T-35) for BOTH
            // fallbacks -- see audio.json ui_effects.player_hurt._why (fx_hurt.wav
            // is referenced by nothing in the asset dump) and ui_effects
            // .enemy_hurt._why (fx_hit_p1.wav is the PLAYER's c01.prefab clip_hit,
            // reused enemy-side because no enemy hurt-clip field exists).
            {
                const std::string &dataClip = m_Data.FindEffectClip(5);
                m_Audio->PlaySfx(!dataClip.empty()
                                     ? dataClip
                                     : UiClip(isPlayer ? "player_hurt" : "enemy_hurt"));
            }
        } else if (e.name == "death") {
            SpawnEffect(EffectFrames(root, "effect04", 23), pos, 600.0F); // explosion puff
            // PORT CHOICE (T-35) for all three rows -- see audio.json
            // ui_effects.player_death._why (c01.prefab declares clip_skill and
            // clip_hit but no death clip, so the port borrows an enemy clip_dead),
            // ui_effects.boss_death._why and ui_effects.enemy_death._why (both are
            // ONE prefab's clip_dead applied to every boss / every mob, because
            // per-species death clips are not ported).
            m_Audio->PlaySfx(
                UiClip(isPlayer ? "player_death" : (isBoss ? "boss_death" : "enemy_death")));
            if (m_Run != nullptr && !isPlayer) {
                m_Run->RecordKill(); // B4: RGGameProcess.kill_count analog
            }
            if (!isPlayer) {
                SpawnReward(isBoss, e.entityId, pos); // B3: RGEController.GetReward
            }
        } else if (e.name.rfind("skill_effect:", 0) == 0) {
            // P3.3.c headless hook: "Skill: <char> effect <cue>" trace for a hero
            // ultimate's world effect (thunder strike, bat swarm, holy light, ...). A cue
            // carrying NOT_PORTED(T-xx) marks an effect the sim cannot express yet.
            // Logging only -- no behaviour.
            LOG_INFO("Skill: {} effect {}", m_CharId, e.name.substr(13));
        } else if (e.name == "skill_use") {
            // P3.3.a headless hook: "Skill: <char> activated" trace, mirrors the
            // "Buff: ... applied" convention (P3.1). NO-OP on behaviour -- logging
            // only; fires once per TryTrigger Activated edge (skill_use is only
            // pushed on that edge, see Simulation::TickSkill). ICharSkill has no
            // public SkillCd() accessor, so the trace reports charId only.
            LOG_INFO("Skill: {} activated", m_CharId);
            // M3e BuffStatue.UseSkill: the worshipped statue's blessing on each skill
            // press, gated by its cd. Priest (statue_index 2, BuffStatue02 cd 8):
            // regain energy; Thief (6): coins from the enemies in the room. The
            // other five (knight summon, daggers, shockwave, missiles, rabbit) need
            // their bullet/summon subsystems -- logged, not applied (PORT gap).
            const int si = m_Run != nullptr ? m_Run->State().statueIndex : 0;
            if (si > 0 && m_StatueCdMs <= 0.0F) {
                CombatStats &st = m_Player->Stats();
                if (si == 2) {
                    m_StatueCdMs = 8000.0F;
                    st.energy = std::min(st.maxEnergy, st.energy + 20); // amount: PORT (unrecovered)
                    LOG_INFO("Statue: Priest blessing -> energy {}", st.energy);
                } else if (si == 6) {
                    m_StatueCdMs = 8000.0F;
                    int gained = 0;
                    for (const auto &ev : evs) {
                        if (ev.alive) {
                            ++gained;
                        }
                    }
                    if (gained > 0) {
                        m_Run->AddCoins(gained);
                    }
                    LOG_INFO("Statue: Thief blessing -> +{} coins", gained);
                } else {
                    LOG_INFO("Statue: {} blessing NOT PORTED (T-14, statue_index {})", kStatueName[si], si);
                }
            }
        } else if (e.name == "skill_dash") {
            // W1 (B1-P4a): the C02 ultimate is a forward dash the sim surfaces as an
            // event for the owner to apply (the sim's player position is an INPUT, not
            // sim-owned). Apply it to the AUTHORITATIVE player transform -- NOT the
            // by-value playerPos copy -- through the SAME BlocksAny wall gate as walking
            // (the shared ApplyDash), so a dash toward a wall is clamped, never tunnels.
            if (isPlayer) {
                Sim::ApplyDash(*this, m_Player->m_Transform.translation, AimDirection(),
                               Sim::kSkillDashPx, m_PlayerRadiusPx);
            }
        }
    }
}

void GameScene::SpawnEffect(std::vector<std::string> frames, glm::vec2 pos, float lifeMs) {
    if (frames.empty()) {
        return;
    }
    int interval = static_cast<int>(lifeMs / static_cast<float>(frames.size()));
    if (interval < 1) {
        interval = 1;
    }
    auto obj = std::make_shared<Util::GameObject>();
    obj->SetDrawable(std::make_shared<Util::Animation>(std::move(frames), /*play=*/true,
                                                       interval, /*looping=*/false));
    obj->SetZIndex(RenderLayout::kZEffect); // above actors (player 5, boss 5.5) and the w2 overlays (6.x)
    // SCALE (FAITHFUL): effect sprites are PPU-16 like everything else.
    obj->m_Transform.scale = {Scale::kSpriteScale, Scale::kSpriteScale};
    obj->m_Transform.translation = pos;
    m_Renderer.AddChild(obj);
    m_Effects.emplace_back(std::move(obj), lifeMs);
}

void GameScene::Render() {
    // P2.17 (c): Camera.backgroundColor = MapManager.camera_bg of this level (FAITHFUL @
    // map_<V><N>.prefab MapManager.camera_bg -> levels.json `camera_bg`; A1 (0.020, 0.122,
    // 0.157), B1 (0.027, 0.103, 0.191)). INFERRED (T-60): the set call into the Camera is in
    // Ghidra-truncated code, but the reference frames show the same navy void. Clearing here
    // (not only via Context::Update's end-of-frame glClear) makes the first frame correct.
    const glm::vec3 bg = RenderLayout::ClearColor(m_Level);
    glClearColor(bg.r, bg.g, bg.b, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Util::SetActiveViewMatrix(m_Camera.GetViewMatrix());
    m_Renderer.Update();
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen-space from here on
    m_Hud.Draw(m_Player->Stats(), m_Run != nullptr ? m_Run->State().stats.coins : 0);
}

} // namespace Game
