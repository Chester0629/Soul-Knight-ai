#include "scenes/GameScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include <glm/glm.hpp>

#include "Core/Context.hpp"

#include "game/FloorClear.hpp"
#include "game/RunController.hpp"
#include "game/EnemyReward.hpp"
#include "game/EnemyMaker.hpp"
#include "entities/CoinPickup.hpp"

#include "world/FloorBlock.hpp"

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
constexpr float kPlayerRadius = 16.0F;
constexpr float kBulletLifeMs = 1500.0F;
// World size (pixels) of one RoomGen grid cell.
constexpr float kCellPx = 32.0F;
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
// SK_AUTOWALK steering speed (px/s): drives headless room->corridor->room traversal.
constexpr float kAutoWalkSpeed = 300.0F;
// Inset (px) from a room edge before the clear-room door lock may arm. The door-
// seal cells occupy the outermost cell ring, and a room's interior rect edge
// coincides with that ring's outer face for ANY room size (rect_edge == -16*w ==
// door-cell outer face). So a body whose CENTRE is within one cell (kCellPx) + its
// radius (kPlayerRadius) of an edge OVERLAPS the seal the instant it snaps shut and
// is pinned at the threshold -- the "air wall" (doc'd: door-seal arms at threshold).
// Arming only once the centre is past that band closes the seal BEHIND the player.
// +1px clears the inclusive circle-vs-AABB tangency at the exact boundary.
constexpr float kLockArmInset = kCellPx + kPlayerRadius + 1.0F; // 49px

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

void PlaySfx(const std::string &root, const std::string &file) {
    // Mix_Chunk is cached in Util::SFX's AssetStore, so re-loading by path is cheap.
    Util::SFX sfx(root + "/audio/" + file);
    sfx.Play();
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
    }
    MapManager::Options mapOpt;
    // MapManager.map_long counts the NORMAL rooms of the walk; the original adds
    // the start room and the special rooms (type1 chest/sell, type2 gate, type3)
    // on top. The port's walk folds specials into one count, so add those slots
    // (PORT CHOICE: +4 = start + gate + chest/sell + badass).
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

    // MAP M2: where the transfer gate stands. Boss floor: the boss room (the gate
    // appears once the boss dies). Mob floor: the walk's type-2 special room
    // (MapManager.type2_list = r_transgete); if the walk rolled none, the room
    // farthest from the start plays the gate room.
    m_GateRoom = bossRoom;
    if (!IsBossFloor(m_FloorIndex)) {
        for (std::size_t i = 0; i < floor.Rooms().size(); ++i) {
            if (floor.Rooms()[i].type == 2) {
                m_GateRoom = static_cast<int>(i);
                break;
            }
        }
    }
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
    std::vector<std::shared_ptr<Util::Image>> floorImgs;
    if (m_Level != nullptr) {
        for (const std::string &f : m_Level->floors) {
            if (!f.empty()) {
                floorImgs.push_back(std::make_shared<Util::Image>(root + "/sprites/tiles/" + f + ".png"));
            }
        }
    }
    if (floorImgs.empty()) {
        floorImgs.push_back(std::make_shared<Util::Image>(root + "/sprites/tiles/floor.png"));
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
    auto wallImg = std::make_shared<Util::Image>(root + "/sprites/" + wallTop + ".png");
    auto wallFaceImg = std::make_shared<Util::Image>(root + "/sprites/" + wallFace + ".png");
    const float wallFaceDy = wallDef != nullptr ? wallDef->faceDy : 0.0F;
    const glm::vec2 floorSz = floorImgs.front()->GetSize();
    const glm::vec2 wallSz = wallImg->GetSize();
    const glm::vec2 wallFaceSz = wallFaceImg->GetSize();
    (void)wallSz;
    (void)wallFaceSz;
    // Design-obstacle sprites keyed by obj_index (RGObjectSkin -> obstacle_list).
    // MAP M1: from THIS level's obstacle_list; pre-M1 obj_N crops as fallback.
    std::array<std::shared_ptr<Util::Image>, 12> objImg{};
    std::array<std::shared_ptr<Util::Image>, 12> objAltImg{}; // the prefab's w2 overlay (crate top ...)
    std::array<float, 12> objAltDy{};
    for (int i : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}) {
        const std::size_t oi = static_cast<std::size_t>(i);
        if (m_Level != nullptr && oi < m_Level->obstacles.size() &&
            !m_Level->obstacles[oi].sprite.empty()) {
            objImg[oi] = std::make_shared<Util::Image>(
                root + "/sprites/tiles/" + m_Level->obstacles[oi].sprite + ".png");
            if (!m_Level->obstacles[oi].alt.empty()) {
                objAltImg[oi] = std::make_shared<Util::Image>(
                    root + "/sprites/tiles/" + m_Level->obstacles[oi].alt + ".png");
                objAltDy[oi] = m_Level->obstacles[oi].altDy;
            }
        } else if (i != 9 && i != 10) {
            objImg[oi] = std::make_shared<Util::Image>(
                root + "/sprites/tiles/obj_" + std::to_string(i) + ".png");
        }
    }
    // The start room repositions the player onto a guaranteed floor cell (set
    // in the loop below); the hardcoded (0,0) spawn can land inside a wall.
    glm::vec2 startFloorPos{0.0F, 0.0F};
    m_Waves.assign(floor.Rooms().size(), SpawnWaves{});
    int roomIndex = 0;
    for (const RoomCell &cell : floor.Rooms()) {
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
        ropt.wallLevel = cell.type >= 2 ? 2 : 1; // richer obstacles in special rooms
        ropt.obstacleLevel = 1;
        // MAP M2: special rooms are EMPTY SHELLS at their prefab size -- r_start
        // 15x15, r_transgete / r_mercenary / r_mineral / r_statue 11x11 (the
        // r1_* design layouts belong to normal rooms only; the type-1 fallback
        // had put crates and speed pads into the portal room). Boss rooms keep
        // the normal-room layout until their 21x21 prefabs are extracted (M3).
        const bool bossShell = roomIndex == bossRoom && IsBossFloor(m_FloorIndex);
        const bool startShell = roomIndex == floor.StartIndex();
        const bool specialShell = (cell.type == 2 || cell.type == 3) && !bossShell;
        if (startShell) {
            ropt.roomWidth = 15;
            ropt.roomHeight = 15;
        } else if (specialShell) {
            ropt.roomWidth = 11;
            ropt.roomHeight = 11;
        }
        if (startShell || specialShell) {
            ropt.proceduralObstacles = false; // bare floor, nothing inside
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
            (cell.roomId.empty() || startShell || specialShell) ? nullptr
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
        m_RoomLife.emplace_back(cell.type);
        m_RoomLife.back().SetSeed(m_FloorSeed + 1 + roomIndex);
        m_RoomLife.back().OpenDoor(); // doors open until StartRoom

        // Corridor-connected floor cells (the door-reachable main region). Spawns
        // anchor here so the player can never start trapped in an obstacle pocket
        // -- RoomGen::FloorList can include cells isolated from every exit. Same
        // source FloorConnectivityTest anchors its reachability assertion on.
        const auto spawnCells = FloorBlock::ConnectedFloorCells(rg);
        if (roomIndex == m_GateRoom && !spawnCells.empty()) {
            // The gate at the room's b_point (its centre): the middle connected cell.
            const auto &gc = spawnCells[spawnCells.size() / 2];
            m_GatePos = Room::CellToWorld(gc.first, gc.second, rg.Width(), rg.Height(),
                                          kCellPx, origin);
        }

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
        const auto addObjSprite = [&](const std::shared_ptr<Util::Image> &img,
                                      int cx, int cy)
            -> std::shared_ptr<Util::GameObject> {
            auto tile = std::make_shared<Util::GameObject>();
            tile->SetDrawable(img);
            tile->SetZIndex(1.0F);
            const glm::vec2 sz = img->GetSize();
            const float scale = kCellPx / 16.0F; // pixelsToUnits 16 -> 2x cell fit
            tile->m_Transform.scale = glm::vec2(scale, scale);
            // Bottom of the sprite at the cell's bottom edge (pivot 0.5,0).
            tile->m_Transform.translation =
                Room::CellToWorld(cx, cy, rg.Width(), rg.Height(), kCellPx, origin) +
                glm::vec2(0.0F, sz.y * scale * 0.5F - kCellPx * 0.5F);
            m_RoomTiles.push_back(tile);
            m_Renderer.AddChild(tile);
            return tile;
        };
        for (int x = 0; x < rg.Width(); ++x) {
            for (int y = 0; y < rg.Height(); ++y) {
                if (Room::IsSolidCell(rg.At(x, y))) {
                    // FAITHFUL @ wall601.prefab: the wall is a 16x24 sprite with pivot
                    // (0.5, 0) standing on its cell (collider 1x1 at +0.5) plus the w2
                    // face overlay at the same spot; a 16-px cell is 1 unit, so the
                    // wall rises 1.5 units. Draw both bottom-anchored at native aspect,
                    // sorted by y so a lower wall overdraws the one behind it.
                    {
                        auto ground = addObjSprite(floorImgs[0], x, y); // ground under the wall
                        ground->SetZIndex(0.1F - ground->m_Transform.translation.y * 1e-5F);
                    }
                    // A design-room obj_index-0 wall wears the level's obstacle_list[0]
                    // skin (wall603 ice crystals / wall_tree1 ...), not the perimeter wall.
                    const bool designWall = std::find(ropt.designSolidCells.begin(),
                                                      ropt.designSolidCells.end(),
                                                      std::make_pair(x, y)) != ropt.designSolidCells.end();
                    if (designWall && objImg[0] != nullptr) {
                        auto dw = addObjSprite(objImg[0], x, y);
                        dw->SetZIndex(1.3F - dw->m_Transform.translation.y * 1e-5F);
                        if (objAltImg[0] != nullptr) {
                            auto dwTop = addObjSprite(objAltImg[0], x, y);
                            dwTop->m_Transform.translation.y += objAltDy[0] * kCellPx;
                            dwTop->SetZIndex(1.31F - dw->m_Transform.translation.y * 1e-5F);
                        }
                        continue;
                    }
                    const auto solidTile = addObjSprite(wallImg, x, y);
                    solidTile->SetZIndex(1.2F - solidTile->m_Transform.translation.y * 1e-5F);
                    if (wallFaceImg != wallImg) {
                        const auto face = addObjSprite(wallFaceImg, x, y);
                        face->m_Transform.translation.y += wallFaceDy * kCellPx;
                        face->SetZIndex(1.21F - face->m_Transform.translation.y * 1e-5F);
                    }
                } else {
                    // MAP M1: the floor variant RoomGen rolled for this cell. Floor
                    // sprites are 16 px wide but may be 24 px tall (castle f2xx: a raised
                    // lip), pivot (0.5,0): draw bottom-anchored at native aspect, upper
                    // rows first, so the lip overlaps the row above instead of squashing.
                    const std::size_t v = static_cast<std::size_t>(rg.FloorVariantAt(x, y));
                    auto ft = addObjSprite(floorImgs[v < floorImgs.size() ? v : 0], x, y);
                    ft->SetZIndex(0.1F - ft->m_Transform.translation.y * 1e-5F);
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
                auto tile = addObjSprite(objImg[oi], o.x, o.y);
                tile->SetZIndex(1.3F - tile->m_Transform.translation.y * 1e-5F);
                if (objAltImg[oi] != nullptr) {
                    // FAITHFUL @ box04.prefab: the w2 overlay (crate top, 16x16) sits
                    // alt_dy units above the 16x8 base -> the crate reads as a box.
                    auto top = addObjSprite(objAltImg[oi], o.x, o.y);
                    top->m_Transform.translation.y += objAltDy[oi] * kCellPx;
                    top->SetZIndex(1.31F - tile->m_Transform.translation.y * 1e-5F);
                }

                const bool box = o.objIndex >= 1 && o.objIndex <= 4;
                const bool brazier = o.objIndex == 11; // conservative blocker (TODO)
                if (box || brazier) {
                    const glm::vec2 wpos = Room::CellToWorld(
                        o.x, o.y, rg.Width(), rg.Height(), kCellPx, origin);
                    ObstacleBox ob;
                    ob.collider = Util::Collider::MakeAABB(
                        wpos, glm::vec2{kCellPx, kCellPx});
                    ob.tile = tile;
                    ob.destructible = box; // boxes (1-4) break; braziers (11) permanent
                    if (box) {
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
        const auto addBlockSprite = [&](const std::shared_ptr<Util::Image> &img, int bx, int by,
                                        float zBase) {
            auto tile = std::make_shared<Util::GameObject>();
            tile->SetDrawable(img);
            const glm::vec2 sz = img->GetSize();
            const float scale = kCellPx / 16.0F;
            tile->m_Transform.scale = glm::vec2(scale, scale);
            tile->m_Transform.translation =
                blockToWorld(bx, by) + glm::vec2(0.0F, sz.y * scale * 0.5F - kCellPx * 0.5F);
            tile->SetZIndex(zBase - tile->m_Transform.translation.y * 1e-5F);
            m_RoomTiles.push_back(tile);
            m_Renderer.AddChild(tile);
            return tile;
        };
        const auto addCorridorWall = [&](int bx, int by) {
            addBlockSprite(floorImgs.front(), bx, by, 0.1F); // ground under the wall
            addBlockSprite(wallImg, bx, by, 1.2F);
            if (wallFaceImg != wallImg) {
                auto face = addBlockSprite(wallFaceImg, bx, by, 1.21F);
                face->m_Transform.translation.y += wallFaceDy * kCellPx;
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
                    addBlockSprite(floorImgs.front(), bx, by, 0.1F); // walkable corridor floor
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
            const glm::vec2 spawn = Room::CellToWorld(
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
                // CHAPTER PROGRESSION: pick from THIS chapter's real boss roster
                // (its biome folder's bossNN prefabs, FAITHFUL @ ExportedProject:
                // ch1 ice = boss09/10/13, ch2 castle = boss01/02, ch3 alien =
                // boss05/06) instead of a fixed cross-chapter 5-slot roster. The
                // pick stays a pure DETERMINISTIC function of (room type, boss-room
                // index) -- nothing is drawn from the per-floor RNG.
                const std::vector<const EnemyDef *> bossPool =
                    m_Data.ChapterBossPool(ChapterOf(m_FloorIndex));
                const EnemyDef *bossDef =
                    bossPool.empty()
                        ? m_Data.FindEnemyByClass("BossAI01") // defensive fallback
                        : bossPool[(static_cast<std::size_t>(cell.type) +
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
                const int bossMaxHp = (bossDef != nullptr && bossDef->maxHp > 0)
                                          ? bossDef->maxHp
                                          : 500;
                auto boss = std::make_shared<Boss>(
                    root, spawn, bossMaxHp, /*shootCd=*/2.0F, m_FloorSeed + 9000,
                    m_Data.SpriteFrames(bossPrefab, root),
                    m_Data.SpriteIntervalMs(bossPrefab, 150));
                boss->SetRoomId(roomIndex);
                m_Bosses.push_back(boss);
                m_Sim->SetBoss(2.0F, spawn, bossMaxHp, roomIndex,
                               m_FloorSeed + 9000, bossId);
                LOG_INFO("BossMaker-lite: room {} spawned {} maxHp={} (type {})", roomIndex,
                         bossId, bossMaxHp, cell.type);
            } else if (cell.type != 1) {
                // MAP M2: every special room prefab (r_start, r_transgete, r_chest,
                // r_sell, r_statue, r_mercenary, r_mineral, the boss rooms) carries an
                // EnemyMaker with level 0 -> StartEnemyMaker spawns NOTHING. Only the
                // r1_* normal rooms (type 1) fight. (type3_list = mercenary/mineral/
                // statue event rooms, not a badass tier.)
                LOG_INFO("EnemyMaker: room {} (type {}) special prefab -> level 0, no spawns",
                         roomIndex, cell.type);
            } else {
                // MAP M1: the original EnemyMaker over the level's REAL spawn pool.
                // FAITHFUL @ MapManager.enemy_list (weighted: duplicates are the
                // weights) + EnemyMaker.StartEnemyMaker (level -> enemy_point budget,
                // big-room bump) + CreatePart (spend the budget, charging each
                // pick's RGEController.consume). Badass (type 3) rooms add one elite
                // from ex_enemy_list (always_badass). The maker draws from its OWN
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
                const int level = EnemyMakerLevel(stage, rg.Width());
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
                // PORT CHOICE: stage * 3 % per pick (chapter-scaled), labelled so a
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
                wv.pending = ids;
                // FAITHFUL: StartEnemyMaker -> this_count = level * 3 + this_count (prefab
                // this_count is 0 on the r1_* rooms): the size of each part.
                wv.waveSize = std::max(1, level * 3);
                SpawnNextWave(roomIndex);
                LOG_INFO("EnemyMaker: room {} (type {}) level {} points {} -> {} planned, wave size {}",
                         roomIndex, cell.type, level, points, ids.size(), wv.waveSize);
            }
        }

        // Place a chest in every non-start room on a floor cell (tier = room
        // type, so special/badass rooms roll richer loot).
        if (roomIndex != floor.StartIndex() && !spawnCells.empty()) {
            const auto &cc = spawnCells[spawnCells.size() / 4];
            const glm::vec2 cpos = Room::CellToWorld(
                cc.first, cc.second, rg.Width(), rg.Height(), kCellPx, origin);
            // MAP M1: chest tier = MapManager.chest_level of the level (RGChest.level),
            // no longer the room type.
            auto chest = std::make_shared<Chest>(
                root, cpos, m_Level != nullptr ? m_Level->chestLevel : cell.type);
            m_Chests.push_back(chest);
            m_Renderer.AddChild(chest);
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
    const std::string equipWeaponId =
        m_Carried ? m_Carried->weaponId : std::string("Gun001");
    if (const WeaponDef *wdef = m_Data.FindWeapon(equipWeaponId)) {
        m_Sim->EquipWeapon(*wdef, equipWeaponId, m_FloorSeed + 5);
        m_WeaponEnergyCost = WeaponEnergyCost(*wdef);
        m_CurrentWeaponId = equipWeaponId;
    } // else: unknown id -> leave the default (never deref a null FindWeapon).

    // --- B1-P4a: dispatch the player skill brain by character id (charId seam).
    // skillCd / inSkillTime come from PlayerTemplate (CharacterDef; one shared template
    // -- per-char overrides pending). charId is carried across floors (defaults "c01"
    // until the HeroRoom picker, B5). C01/C02 effects are faithful; c03..c13 are
    // gate+cooldown-only stubs. Rebuilt per floor (starts ready).
    // charId + cdef were resolved at player-creation time (top of OnEnter).
    m_Sim->SetPlayerSkill(charId,
                          cdef != nullptr ? cdef->skillCd : m_Data.PlayerTemplate().skillCd,
                          cdef != nullptr ? cdef->inSkillTime
                                          : m_Data.PlayerTemplate().inSkillTime);

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
    if (const char *gt = std::getenv("SK_GATE_TELEPORT")) {
        m_GateTeleportFrame = std::atol(gt);
    }
    if (const char *gb = std::getenv("SK_GATE_BESIDE")) {
        m_GateBesideFrame = std::atol(gb);
    }
    if (const char *fs = std::getenv("SK_FORCE_SKILL")) {
        m_ForceSkillFrame = std::atol(fs);
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
    m_Camera.SetZoom(48.0F / kCellPx);
    LOG_INFO("GameScene: multi-room floor ready");
}

void GameScene::OnExit() {
    // Fires when the SceneManager pops/replaces this scene (DoPop -> OnExit before
    // destruction). Logging it proves the dying floor is EXITED on a death/clear
    // Replace -- not Pushed-over-and-leaked at the bottom of the stack (step 2b).
    LOG_INFO("Floor {} GameScene OnExit (scene torn down, not leaked)", m_FloorIndex);
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
            if (ob.tile != nullptr) {
                ob.tile->SetVisible(false); // hide the broken box's placeholder skin
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
    // SK_FORCE_DIE=K test hook (env, NO-OP if unset): kill the player on frame K to
    // drive the death->EndScene path headlessly (no combat needed). Runs BEFORE the
    // death check below.
    if (m_ForceDieFrame >= 0 && m_Frame >= m_ForceDieFrame) {
        m_Player->Stats().hp = 0;
    }
    if (m_ForceKillFrame >= 0 && m_Frame == m_ForceKillFrame && m_Sim) {
        m_Sim->KillAllHostiles(); // SK_FORCE_KILL test hook: NO-OP unless set
    }
    if (m_GateTeleportFrame >= 0 && m_Frame == m_GateTeleportFrame) {
        m_Player->m_Transform.translation = m_GatePos; // SK_GATE_TELEPORT test hook
    }
    if (m_GateBesideFrame >= 0 && m_Frame == m_GateBesideFrame) {
        m_Player->m_Transform.translation = m_GatePos + glm::vec2{0.0F, -64.0F}; // SK_GATE_BESIDE
    }

    // --- Player movement with axis-separated wall sliding (stays scene-side) ---
    const glm::vec2 before = m_Player->Position();
    m_Player->Update(dtMs);
    glm::vec2 after = m_Player->Position();
    if (m_AutoWalk) { // SK_AUTOWALK: override input with a steer toward the target
        const glm::vec2 d = m_AutoWalkTarget - before;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > 1.0F) {
            after = before + (d / len) * (kAutoWalkSpeed * dtMs / 1000.0F);
        }
    }
    glm::vec2 resolved = before;
    if (!BlocksAny(glm::vec2(after.x, before.y), kPlayerRadius)) {
        resolved.x = after.x;
    }
    if (!BlocksAny(glm::vec2(resolved.x, after.y), kPlayerRadius)) {
        resolved.y = after.y;
    }
    m_Player->m_Transform.translation = resolved;

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
    Sim::WorldInputs in;
    in.playerPos = resolved;
    in.aimDir = AimDirection();
    in.firing = Util::Input::IsKeyPressed(Util::Keycode::MOUSE_LB) &&
                m_Player->Stats().energy >= m_WeaponEnergyCost;
    in.skill = Util::Input::IsKeyDown(Util::Keycode::MOUSE_RB) || forceSkill;
    if (forceSkill) {
        in.firing = true; // SK_FORCE_SKILL hook: hold fire so the active-skill mirror is observable.
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
    in.playerAlive = !m_Player->Stats().IsDead();
    in.playerRoomId = playerRoomId;

    m_Sim->SetPlayerStats(m_Player->Stats());      // push current vitals in
    m_Sim->Advance(dtMs, in);                       // sim damages its player-stats copy
    const CombatStats &simPlayer = m_Sim->PlayerStats();
    // MARKER (decision C): pull back ONLY hp + armor. Energy is scene-owned (regen + spend
    // below); the sim never modifies energy, so do NOT copy simPlayer.energy back -- doing so
    // would clobber this frame's regen/spend with the value we pushed in. Intentional asymmetry.
    m_Player->Stats().hp = simPlayer.hp;
    m_Player->Stats().armor = simPlayer.armor;
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
    // hash safe); only this LOCK gate is inset to kLockArmInset px deep. Entering
    // the rect (wake) and committing deep enough to lock are now two thresholds.
    if (playerRoomId >= 0 &&
        m_RoomLife[static_cast<std::size_t>(playerRoomId)].State() ==
            RGRoomX::Process::Uncleared &&
        m_Rooms[static_cast<std::size_t>(playerRoomId)]->ContainsPointInset(
            resolved, kLockArmInset) &&
        RoomHasLiveHostile(playerRoomId)) {
        m_RoomLife[static_cast<std::size_t>(playerRoomId)].StartRoom();
        LOG_INFO("RGRoomX: room {} StartRoom -> Active (door_open=0, sealed)",
                 playerRoomId);
    }
    for (std::size_t i = 0; i < m_RoomLife.size(); ++i) {
        // MAP M1: a room with parts still queued is NOT clear -- the next part
        // spawns 0.8 s after the previous one died (EnemyMaker.EnemyDead ->
        // Invoke("CreatePart", 0.8f)).
        if (m_RoomLife[i].State() == RGRoomX::Process::Active &&
            !RoomHasLiveHostile(static_cast<int>(i)) && i < m_Waves.size() &&
            !m_Waves[i].pending.empty()) {
            SpawnWaves &wv = m_Waves[i];
            if (wv.timerMs < 0.0F) {
                wv.timerMs = 800.0F;
            } else {
                wv.timerMs -= dtMs;
                if (wv.timerMs <= 0.0F) {
                    wv.timerMs = -1.0F;
                    SpawnNextWave(static_cast<int>(i));
                }
            }
            continue;
        }
        if (m_RoomLife[i].State() == RGRoomX::Process::Active &&
            !RoomHasLiveHostile(static_cast<int>(i))) {
            m_RoomLife[i].ClearRoom(); // process=Cleared, reward gate, OpenDoor
            if (m_RoomLife[i].RewardGranted()) {
                // reward gate FIRED (room_type==1). Gate lands here; the actual
                // reward CONTENT (loot overlay) is the unverified special-room debt.
                LOG_INFO("RGRoomX: room {} cleared -> reward gate fired (room_type==1)",
                         i);
            }
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
        if (!ev.alive) {
            m_Renderer.RemoveChild(view);
            view = nullptr;
        }
    }
    if (!m_Bosses.empty() && m_Bosses[0] != nullptr && m_Sim->HasBoss()) {
        const Sim::Simulation::EntityView bv = m_Sim->BossView();
        m_Bosses[0]->m_Transform.translation = bv.pos;
        if (!bv.alive) {
            m_Renderer.RemoveChild(m_Bosses[0]);
            m_Bosses[0] = nullptr;
            LOG_INFO("Boss defeated!");
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
        const std::string dropId = m_Loot.Roll(chest->Tier(), m_Rng);
        if (const WeaponDef *def = m_Data.ResolveDropWeapon(dropId)) {
            auto pickup = std::make_shared<WeaponPickup>(
                std::string(RESOURCE_DIR),
                chest->Position() + glm::vec2(0.0F, -kCellPx), def);
            m_Pickups.push_back(pickup);
            m_Renderer.AddChild(pickup);
        }
    }

    // --- Pickups: walk over one to equip it (rebuilds the sim WeaponController) ---
    for (auto it = m_Pickups.begin(); it != m_Pickups.end();) {
        if (glm::distance(playerPos, (*it)->Position()) <= kPickupRange) {
            const WeaponDef *def = (*it)->Def();
            ++m_WeaponSwaps;
            m_Sim->EquipWeapon(*def, def->id, m_FloorSeed + 5 + m_WeaponSwaps);
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
            m_Renderer.RemoveChild(*it);
            it = m_Coins.erase(it);
        } else {
            ++it;
        }
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
         m_Waves[static_cast<std::size_t>(m_GateRoom)].pending.empty()) &&
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
        m_GateLight->SetZIndex(1.5F);
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
        m_Gate->SetZIndex(2.0F);
        m_Renderer.AddChild(m_Gate);
        LOG_INFO("TransferGate: open in room {} at ({:.0f},{:.0f})", m_GateRoom, m_GatePos.x,
                 m_GatePos.y);
    }
    const bool gateEntered =
        m_GateActive && glm::distance(playerPos, m_GatePos) <= kGateTriggerPx;
    if (gateEntered && !m_Transitioning) {
        LOG_INFO("TransferGate: entered -> next floor");
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
    const int seed = m_FloorSeed + 1000 + roomIndex + serial * 131;
    auto enemy = std::make_shared<Enemy>(*rdef, root, at, 450.0F, 260.0F,
                                         m_Data.SpriteFrames(rdef->id, root),
                                         m_Data.SpriteIntervalMs(rdef->id, 120));
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
    if (wv.pending.empty() || wv.points.empty()) {
        wv.pending.clear();
        return;
    }
    const int n = std::min(wv.waveSize, static_cast<int>(wv.pending.size()));
    for (int k = 0; k < n; ++k) {
        const std::string id = wv.pending.front();
        wv.pending.erase(wv.pending.begin());
        // Spread this part over the room's spawn points (serial-indexed stride).
        const std::size_t pi = (wv.points.size() * static_cast<std::size_t>(wv.serial * 7 + 3)) %
                               wv.points.size();
        SpawnEnemy(id, wv.points[pi], roomIndex, wv.serial);
        ++wv.serial;
    }
    LOG_INFO("EnemyMaker: room {} part spawned {} ({} pending)", roomIndex, n, wv.pending.size());
}

bool GameScene::PendingWaves() const {
    for (const SpawnWaves &w : m_Waves) {
        if (!w.pending.empty()) {
            return true;
        }
    }
    return false;
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
    // FAITHFUL: RGEController.GetReward -- rg_random.Range(0,100) < reward_rate, then
    // reward_value[4] counts of [gold, silver, copper, energy].
    const std::vector<RewardDrop> drops = RollEnemyReward(*def, m_RewardRng);
    if (drops.empty()) {
        return;
    }
    const std::string root(RESOURCE_DIR);
    int coinsTotal = 0;
    int energyTotal = 0;
    for (const RewardDrop &d : drops) {
        // Scatter direction: a deterministic angle per drop (RGCoin.GetForce).
        const float ang = m_RewardRng.Range(0.0F, 6.2831853F);
        auto coin = std::make_shared<CoinPickup>(root, pos, d.kind,
                                                 glm::vec2{std::cos(ang), std::sin(ang)});
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

        if (e.name == "fire" || e.name == "attack") {
            SpawnEffect({root + "/sprites/effect01.png"}, pos, 80.0F); // muzzle flash
        } else if (e.name == "hurt") {
            SpawnEffect(EffectFrames(root, "effect03", 3), pos, 180.0F); // hit spark
            PlaySfx(root, isPlayer ? "fx_hurt.wav" : "fx_hit_p1.wav");
        } else if (e.name == "death") {
            SpawnEffect(EffectFrames(root, "effect04", 23), pos, 600.0F); // explosion puff
            PlaySfx(root, isBoss ? "fx_boss1_dead.wav" : "enermy_die3.wav");
            if (m_Run != nullptr && !isPlayer) {
                m_Run->RecordKill(); // B4: RGGameProcess.kill_count analog
            }
            if (!isPlayer) {
                SpawnReward(isBoss, e.entityId, pos); // B3: RGEController.GetReward
            }
        } else if (e.name == "skill_dash") {
            // W1 (B1-P4a): the C02 ultimate is a forward dash the sim surfaces as an
            // event for the owner to apply (the sim's player position is an INPUT, not
            // sim-owned). Apply it to the AUTHORITATIVE player transform -- NOT the
            // by-value playerPos copy -- through the SAME BlocksAny wall gate as walking
            // (the shared ApplyDash), so a dash toward a wall is clamped, never tunnels.
            if (isPlayer) {
                Sim::ApplyDash(*this, m_Player->m_Transform.translation, AimDirection(),
                               Sim::kSkillDashPx, kPlayerRadius);
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
    obj->SetZIndex(7.0F); // above actors (player 5, boss 6)
    obj->m_Transform.translation = pos;
    m_Renderer.AddChild(obj);
    m_Effects.emplace_back(std::move(obj), lifeMs);
}

void GameScene::Render() {
    Util::SetActiveViewMatrix(m_Camera.GetViewMatrix());
    m_Renderer.Update();
    Util::SetActiveViewMatrix(glm::mat4(1.0F)); // screen-space from here on
    m_Hud.Draw(m_Player->Stats(), m_Run != nullptr ? m_Run->State().stats.coins : 0);
}

} // namespace Game
