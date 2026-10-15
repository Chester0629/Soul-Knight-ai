#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "data/GameData.hpp"
#include "game/WorldScale.hpp"
#include "sim/Depenetration.hpp"
#include "world/FloorBlock.hpp"
#include "world/Room.hpp"
#include "world/RoomBlockers.hpp"
#include "world/RoomGen.hpp"

using Game::BlockerFromDef;
using Game::ColliderDef;
using Game::DesignObstacleCollider;
using Game::FilterFreeCells;
using Game::FirstBlocker;
using Game::FloorBlock;
using Game::GameData;
using Game::ResolveSpawnPoint;
using Game::Room;
using Game::RoomGen;
using Util::Collider;

// NOLINTBEGIN(readability-magic-numbers)

// P2.18: blocker collider SHAPES from prefab data (AC6) and the spawn / start / boss point
// filters (AC3). Everything runs on the REAL data tables (levels / design rooms / boss rooms /
// statues / machines), not on invented shapes.

namespace {
const char *kResourceRoot = RESOURCE_DIR;
constexpr float kCell = Game::Scale::kCellPx;
constexpr float kHeroR = 12.8F;

ColliderDef Box(float sx, float sy, float ox, float oy) {
    ColliderDef d;
    d.shape = ColliderDef::Shape::Box;
    d.sizeX = sx;
    d.sizeY = sy;
    d.offsetX = ox;
    d.offsetY = oy;
    return d;
}
ColliderDef Circle(float r, float ox, float oy) {
    ColliderDef d;
    d.shape = ColliderDef::Shape::Circle;
    d.radius = r;
    d.offsetX = ox;
    d.offsetY = oy;
    return d;
}

void ExpectBox(const std::vector<ColliderDef> &bl, float sx, float sy, float ox, float oy, const std::string &what) {
    ASSERT_EQ(bl.size(), 1u) << what;
    EXPECT_EQ(bl[0].shape, ColliderDef::Shape::Box) << what;
    EXPECT_FLOAT_EQ(bl[0].sizeX, sx) << what;
    EXPECT_FLOAT_EQ(bl[0].sizeY, sy) << what;
    EXPECT_FLOAT_EQ(bl[0].offsetX, ox) << what;
    EXPECT_FLOAT_EQ(bl[0].offsetY, oy) << what;
    EXPECT_FALSE(bl[0].trigger) << what;
}
void ExpectCircle(const std::vector<ColliderDef> &bl, float r, float ox, float oy, const std::string &what) {
    ASSERT_EQ(bl.size(), 1u) << what;
    EXPECT_EQ(bl[0].shape, ColliderDef::Shape::Circle) << what;
    EXPECT_FLOAT_EQ(bl[0].radius, r) << what;
    EXPECT_FLOAT_EQ(bl[0].offsetX, ox) << what;
    EXPECT_FLOAT_EQ(bl[0].offsetY, oy) << what;
}

bool CircleHits(const Collider &c, glm::vec2 p, float r) {
    return Util::Overlap(c, Collider::MakeCircle(p, r));
}
} // namespace

// ---------------------------------------------------------------------------------------------
// BlockerFromDef: the cell-bottom root rule.
// ---------------------------------------------------------------------------------------------
TEST(RoomBlockersTest, BlockerFromDefPlacesThePrefabColliderOnTheCellBottomRoot) {
    const glm::vec2 cell{320.0F, -64.0F};
    // wall601 / box02: 1x1 at (0, 0.5) == exactly the cell.
    const auto wall = BlockerFromDef(Box(1, 1, 0, 0.5F), cell, kCell);
    ASSERT_TRUE(wall.has_value());
    EXPECT_EQ(wall->center, cell);
    EXPECT_EQ(wall->halfSize, (glm::vec2{16.0F, 16.0F}));
    // statue_0N: 2x2 at (0, 1): four cells, bottom edge on the cell's bottom edge.
    const auto statue = BlockerFromDef(Box(2, 2, 0, 1), cell, kCell);
    ASSERT_TRUE(statue.has_value());
    EXPECT_EQ(statue->center, (cell + glm::vec2{0.0F, 16.0F}));
    EXPECT_EQ(statue->halfSize, (glm::vec2{32.0F, 32.0F}));
    EXPECT_FLOAT_EQ(statue->Min().y, cell.y - 16.0F) << "bottom edge == the cell's bottom edge";
    // obj22_09: 1.5x1.5 at (0, 0.75).
    const auto big = BlockerFromDef(Box(1.5F, 1.5F, 0, 0.75F), cell, kCell);
    ASSERT_TRUE(big.has_value());
    EXPECT_EQ(big->center, (cell + glm::vec2{0.0F, 8.0F}));
    EXPECT_EQ(big->halfSize, (glm::vec2{24.0F, 24.0F}));
    // signs01: circle r 0.5 at (0, 1.25) -> centre 24 px above the cell centre, radius 16.
    const auto sign = BlockerFromDef(Circle(0.5F, 0, 1.25F), cell, kCell);
    ASSERT_TRUE(sign.has_value());
    EXPECT_EQ(sign->shape, Util::ColliderShape::Circle);
    EXPECT_EQ(sign->center, (cell + glm::vec2{0.0F, 24.0F}));
    EXPECT_FLOAT_EQ(sign->radius, 16.0F);
    // degenerate defs yield nothing
    EXPECT_FALSE(BlockerFromDef(ColliderDef{}, cell, kCell).has_value());
    EXPECT_FALSE(BlockerFromDef(Box(0, 1, 0, 0), cell, kCell).has_value());
    EXPECT_FALSE(BlockerFromDef(Circle(0, 0, 0), cell, kCell).has_value());
    ColliderDef trig = Box(1, 1, 0, 0.5F);
    trig.trigger = true;
    EXPECT_FALSE(FirstBlocker({trig}, cell, kCell).has_value()) << "triggers never block";
}

// ---------------------------------------------------------------------------------------------
// AC6: the RC4 shapes come from the prefab data.
// ---------------------------------------------------------------------------------------------
TEST(RoomBlockersTest, DesignObstacleShapesComeFromPrefabData) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    // (level, obj_index) -> prefab values (units; rgprefab/room/obj/*.prefab BoxCollider2D / CircleCollider2D)
    const auto L = [&](const char *id) -> const Game::LevelDef & {
        const Game::LevelDef *l = gd.FindLevel(id);
        EXPECT_NE(l, nullptr) << id;
        return *l;
    };
    // C1: obj11_01 circle r 0.5 @ (0, 1.4); obj22_01 box 2x1.5 @ (0, 0.75); signs01 circle r 0.5 @ (0, 1.25)
    ExpectCircle(L("C1").obstacles[8].blockers, 0.5F, 0.0F, 1.4F, "C1 obj11_01");
    ExpectBox(L("C1").obstacles[9].blockers, 2.0F, 1.5F, 0.0F, 0.75F, "C1 obj22_01");
    ExpectCircle(L("C1").obstacles[10].blockers, 0.5F, 0.0F, 1.25F, "C1 signs01");
    // D1: obj11_02 has NO collider (decor); obj22_02 1.5x1.5 @ (0, 0.75)
    EXPECT_TRUE(L("D1").obstacles[8].blockers.empty());
    ExpectBox(L("D1").obstacles[9].blockers, 1.5F, 1.5F, 0.0F, 0.75F, "D1 obj22_02");
    // E1: obj11_03 box 1x1.2 @ (0, 0.6); obj22_03 1.5x1.5
    ExpectBox(L("E1").obstacles[8].blockers, 1.0F, 1.2F, 0.0F, 0.6F, "E1 obj11_03");
    ExpectBox(L("E1").obstacles[9].blockers, 1.5F, 1.5F, 0.0F, 0.75F, "E1 obj22_03");
    // F1: obj11_07 1x1; obj22_10 1.5x1.5
    ExpectBox(L("F1").obstacles[8].blockers, 1.0F, 1.0F, 0.0F, 0.5F, "F1 obj11_07");
    ExpectBox(L("F1").obstacles[9].blockers, 1.5F, 1.5F, 0.0F, 0.75F, "F1 obj22_10");
    // A1: obj11_04 1x1 (layer 0 Default, still blocks a Body), obj22_05 1x1
    ExpectBox(L("A1").obstacles[8].blockers, 1.0F, 1.0F, 0.0F, 0.5F, "A1 obj11_04");
    // boxes / casks / braziers are the standard 1x1, spikes and speed pads have none
    ExpectBox(L("A1").obstacles[1].blockers, 1.0F, 1.0F, 0.0F, 0.5F, "A1 box02");
    ExpectBox(L("B1").obstacles[11].blockers, 1.0F, 1.0F, 0.0F, 0.5F, "B1 brazier01");
    EXPECT_TRUE(L("A1").obstacles[5].blockers.empty()) << "sting01 spikes are trigger-only";
    EXPECT_TRUE(L("A1").obstacles[6].blockers.empty()) << "speed pads are trigger-only";
}

TEST(RoomBlockersTest, BossRoomPrefabsStatuesAndMachinesComeFromPrefabData) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const auto P = [&](const char *name) -> const Game::PrefabSkin & {
        const Game::PrefabSkin *p = gd.FindPrefabSkin(name);
        EXPECT_NE(p, nullptr) << name;
        return *p;
    };
    ExpectBox(P("obj22_10").blockers, 1.5F, 1.5F, 0.0F, 0.75F, "obj22_10");
    ExpectBox(P("obj11_05").blockers, 1.0F, 1.2F, 0.0F, 0.6F, "obj11_05");
    ExpectBox(P("box_weapon").blockers, 1.7F, 1.0F, -0.15F, 0.5F, "box_weapon");
    ExpectBox(P("brazier03").blockers, 1.0F, 1.0F, 0.0F, 0.5F, "brazier03");
    ASSERT_EQ(gd.Statues().size(), 7u);
    for (const Game::StatueDef &s : gd.Statues()) {
        // statue_0N.prefab `collider` child: Box 2x2 at (0, 1) -- four cells, not one
        ExpectBox(s.blockers, 2.0F, 2.0F, 0.0F, 1.0F, s.id);
    }
    const Game::MachineDef *mi = gd.FindMachine("vending_machine_i");
    const Game::MachineDef *mw = gd.FindMachine("vending_machine_w");
    const Game::MachineDef *wd = gd.FindMachine("water_dispenser");
    ASSERT_TRUE(mi != nullptr && mw != nullptr && wd != nullptr);
    ExpectBox(mi->blockers, 2.0F, 1.0F, 0.0F, 0.5F, "vending_machine_i");
    ExpectBox(mw->blockers, 2.5F, 1.0F, 0.0F, 0.5F, "vending_machine_w");
    ExpectBox(wd->blockers, 1.0F, 1.0F, 0.0F, 0.5F, "water_dispenser");
}

TEST(RoomBlockersTest, DesignObstacleColliderGivesIndices8_9_10TheirCollidersAndNeverBlocksPads) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const glm::vec2 cell{0.0F, 0.0F};
    int propsChecked = 0;
    for (const Game::LevelDef &lv : gd.Levels()) {
        if (lv.obstacles.size() < 12) {
            continue;
        }
        for (int idx : {8, 9, 10}) {
            const auto col = DesignObstacleCollider(&lv, idx, cell, kCell);
            const bool hasData = !lv.obstacles[static_cast<std::size_t>(idx)].blockers.empty();
            EXPECT_EQ(col.has_value(), hasData) << lv.id << " idx " << idx;
            if (col.has_value()) {
                ++propsChecked;
                // the collider is solid for a body on the object: it overlaps a body at its own centre
                EXPECT_TRUE(CircleHits(*col, col->center, kHeroR)) << lv.id << " idx " << idx;
            }
        }
        for (int idx : {1, 2, 3, 4, 11}) {
            EXPECT_TRUE(DesignObstacleCollider(&lv, idx, cell, kCell).has_value()) << lv.id << " idx " << idx;
        }
        for (int idx : {0, 5, 6, 7}) {
            EXPECT_FALSE(DesignObstacleCollider(&lv, idx, cell, kCell).has_value()) << lv.id << " idx " << idx;
        }
    }
    EXPECT_GT(propsChecked, 70) << "8/9/10 exist on every level but D1-D5 idx 8";
    // no level data: the old behaviour (boxes / braziers 32x32, props non-blocking)
    EXPECT_TRUE(DesignObstacleCollider(nullptr, 1, cell, kCell).has_value());
    EXPECT_TRUE(DesignObstacleCollider(nullptr, 11, cell, kCell).has_value());
    EXPECT_FALSE(DesignObstacleCollider(nullptr, 8, cell, kCell).has_value());
    EXPECT_FALSE(DesignObstacleCollider(nullptr, 10, cell, kCell).has_value());
}

TEST(RoomBlockersTest, EveryDesignRoomObjIndex8PlacementBlocksABodyOnIt) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    int placements = 0;
    for (const auto &entry : gd.DesignRooms()) {
        for (const Game::DesignObstacle &o : entry.second.obstacles) {
            if (o.objIndex < 8 || o.objIndex > 10) {
                continue;
            }
            for (const char *lid : {"A1", "C1", "E1", "F1"}) {
                const Game::LevelDef *lv = gd.FindLevel(lid);
                ASSERT_NE(lv, nullptr);
                const glm::vec2 wpos = Room::CellToWorld(o.x, o.y, entry.second.width, entry.second.height, kCell);
                const auto col = DesignObstacleCollider(lv, o.objIndex, wpos, kCell);
                ASSERT_TRUE(col.has_value()) << entry.first << " idx " << o.objIndex << " level " << lid;
                EXPECT_TRUE(CircleHits(*col, col->center, kHeroR));
                ++placements;
            }
        }
    }
    EXPECT_GT(placements, 0) << "design_rooms.json must contain obj_index 8/9/10 markers";
    std::printf("[RoomBlockersTest] design-room obj_index 8/9/10 placements now colliding: %d (x4 levels)\n", placements);
}

// ---------------------------------------------------------------------------------------------
// AC3: spawn / start / boss points never overlap a blocker (sweep over the real rooms).
// ---------------------------------------------------------------------------------------------
namespace {
struct RoomBlockerSet {
    std::vector<Collider> all; // walls + obstacle overlay colliders (alive, static)
    bool Blocks(glm::vec2 p, float r) const {
        const Collider c = Collider::MakeCircle(p, r);
        for (const Collider &b : all) {
            if (Util::Overlap(b, c)) {
                return true;
            }
        }
        return false;
    }
};

void AddDoorSeal(RoomBlockerSet &set, const RoomGen &rg) {
    for (const auto &c : FloorBlock::DoorSealCells(rg)) {
        set.all.push_back(Collider::MakeAABB(Room::CellToWorld(c.first, c.second, rg.Width(), rg.Height(), kCell),
                                             glm::vec2{kCell, kCell}));
    }
}

// What GameScene::OnEnter builds for a normal design room: perimeter + design walls (grid) plus
// the obj_index obstacle overlay colliders on interior cells.
RoomBlockerSet BuildDesignRoom(const GameData &gd, const Game::DesignRoomDef &def, const Game::LevelDef *lv,
                               int seed, RoomGen &outGen) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = def.width;
    o.roomHeight = def.height;
    o.wallLevel = 1;
    o.obstacleLevel = 1;
    o.proceduralObstacles = false;
    for (const auto &ob : def.obstacles) {
        if (ob.objIndex == 0) {
            o.designSolidCells.emplace_back(ob.x, ob.y);
        }
    }
    outGen = RoomGen(seed, {1, 1, 1, 1}, o);
    RoomBlockerSet set;
    const Room room = Room::FromRoomGen(outGen, kCell, glm::vec2{0.0F, 0.0F});
    set.all = room.Walls();
    AddDoorSeal(set, outGen); // GameScene::SpawnBlocked also counts the (open) door-seal band
    for (const auto &ob : def.obstacles) {
        if (ob.x < 0 || ob.x >= outGen.Width() || ob.y < 0 || ob.y >= outGen.Height()) {
            continue;
        }
        const int code = outGen.At(ob.x, ob.y);
        if (code == -1 || code == -2 || code == 11) {
            continue;
        }
        const glm::vec2 wpos = Room::CellToWorld(ob.x, ob.y, outGen.Width(), outGen.Height(), kCell);
        if (const auto col = DesignObstacleCollider(lv, ob.objIndex, wpos, kCell)) {
            set.all.push_back(*col);
        }
    }
    (void)gd;
    return set;
}

glm::vec2 CellPos(const RoomGen &rg, std::pair<int, int> c) {
    return Room::CellToWorld(c.first, c.second, rg.Width(), rg.Height(), kCell);
}
} // namespace

TEST(RoomBlockersTest, SpawnPoolSweepNoPointOverlapsABlockerAfterFilteringAndReportsBefore) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    ASSERT_EQ(gd.DesignRooms().size(), 108u);
    long roomsChecked = 0;
    long seedsChecked = 0;
    long cellsRaw = 0;
    long cellsBlockedBefore = 0;
    long cellsAfterBlocked = 0;
    long picksBefore = 0;
    long picksBlockedBefore = 0;
    long picksBlockedAfter = 0;
    long emptyAfter = 0;
    std::size_t levelsUsed = 0;
    for (const char *lid : {"A1", "A4", "B1", "C1", "C4", "D1", "E1", "F1"}) {
        const Game::LevelDef *lv = gd.FindLevel(lid);
        ASSERT_NE(lv, nullptr) << lid;
        ++levelsUsed;
        for (const auto &entry : gd.DesignRooms()) {
            for (int seed : {1, 2}) {
                RoomGen rg(1, {1, 1, 1, 1}, RoomGen::Options{});
                const RoomBlockerSet set = BuildDesignRoom(gd, entry.second, lv, seed, rg);
                const auto raw = FloorBlock::ConnectedFloorCells(rg);
                const auto blocks = [&set](glm::vec2 p, float r) { return set.Blocks(p, r); };
                const auto kept = FilterFreeCells(raw, rg.Width(), rg.Height(), kCell, glm::vec2{0.0F, 0.0F},
                                                  kHeroR, blocks);
                ++roomsChecked;
                ++seedsChecked;
                cellsRaw += static_cast<long>(raw.size());
                for (const auto &c : raw) {
                    if (set.Blocks(CellPos(rg, c), kHeroR)) {
                        ++cellsBlockedBefore;
                    }
                }
                for (const auto &c : kept) {
                    if (set.Blocks(CellPos(rg, c), kHeroR)) {
                        ++cellsAfterBlocked;
                    }
                }
                if (kept.empty()) {
                    ++emptyAfter;
                    continue;
                }
                // The scene's spawn picks: SpawnWaveBatch index (serial*7+3) % n, serial 0..7.
                for (std::size_t serial = 0; serial < 8; ++serial) {
                    const auto rawPick = raw[(serial * 7 + 3) % raw.size()];
                    const auto keptPick = kept[(serial * 7 + 3) % kept.size()];
                    ++picksBefore;
                    if (set.Blocks(CellPos(rg, rawPick), kHeroR)) {
                        ++picksBlockedBefore;
                    }
                    if (set.Blocks(CellPos(rg, keptPick), kHeroR)) {
                        ++picksBlockedAfter;
                    }
                }
            }
        }
    }
    std::printf("[RoomBlockersTest] spawn sweep: %zu levels x 108 design rooms x 2 seeds = %ld room builds; "
                "floor cells %ld, centre-blocked before %ld after %ld; wave picks %ld, blocked before %ld after %ld; "
                "rooms left with no free cell %ld\n",
                levelsUsed, roomsChecked, cellsRaw, cellsBlockedBefore, cellsAfterBlocked, picksBefore,
                picksBlockedBefore, picksBlockedAfter, emptyAfter);
    EXPECT_EQ(roomsChecked, 8L * 108L * 2L);
    EXPECT_GT(cellsBlockedBefore, 0L) << "the sweep must reproduce the bug: raw spawn pools DO contain blocked cells";
    EXPECT_GT(picksBlockedBefore, 0L) << "... and so do the scene's actual wave picks";
    EXPECT_EQ(cellsAfterBlocked, 0L) << "no kept spawn cell may overlap a blocker";
    EXPECT_EQ(picksBlockedAfter, 0L) << "no wave pick may overlap a blocker";
    EXPECT_EQ(emptyAfter, 0L) << "every design room keeps at least one free spawn cell";
}

TEST(RoomBlockersTest, BigSpeciesAreReseatedToAFreeCellOfTheSameRoom) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    const Game::LevelDef *lv = gd.FindLevel("B1");
    ASSERT_NE(lv, nullptr);
    long reseated = 0;
    long unresolved = 0;
    long total = 0;
    for (const auto &entry : gd.DesignRooms()) {
        RoomGen rg(1, {1, 1, 1, 1}, RoomGen::Options{});
        const RoomBlockerSet set = BuildDesignRoom(gd, entry.second, lv, 1, rg);
        const auto raw = FloorBlock::ConnectedFloorCells(rg);
        const auto blocks = [&set](glm::vec2 p, float r) { return set.Blocks(p, r); };
        const auto kept = FilterFreeCells(raw, rg.Width(), rg.Height(), kCell, glm::vec2{0.0F, 0.0F}, kHeroR, blocks);
        std::vector<glm::vec2> pts;
        for (const auto &c : kept) {
            pts.push_back(CellPos(rg, c));
        }
        ASSERT_FALSE(pts.empty());
        for (const float bodyR : {19.2F, 25.6F, 32.0F}) { // boss01-10 / ex_slime01 / boss11 body circles
            for (std::size_t serial = 0; serial < 8; ++serial) {
                ++total;
                const glm::vec2 want = pts[(serial * 7 + 3) % pts.size()];
                const glm::vec2 got = ResolveSpawnPoint(pts, want, bodyR, blocks);
                if (got != want) {
                    ++reseated;
                }
                if (set.Blocks(got, bodyR)) {
                    ++unresolved; // no roomy cell: the scene then depenetrates (GameScene::SpawnEnemy)
                    bool ok = false;
                    const glm::vec2 pushed = Game::Sim::DepenetrateAgainst(set.all, got, bodyR, &ok);
                    EXPECT_TRUE(ok);
                    EXPECT_FALSE(set.Blocks(pushed, bodyR)) << "depenetration seats it where its body is free";
                }
            }
        }
    }
    std::printf("[RoomBlockersTest] big-body re-seat: %ld/%ld picks moved to a roomier cell, %ld left to the "
                "depenetration fallback\n", reseated, total, unresolved);
    EXPECT_GT(reseated, 0L);
}

TEST(RoomBlockersTest, BossRoomSpawnCellsSweepNoPointOverlapsABlocker) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    ASSERT_EQ(gd.BossRooms().size(), 14u);
    long rooms = 0;
    long before = 0;
    long after = 0;
    long middlePickBefore = 0;
    long middlePickAfter = 0;
    for (const auto &entry : gd.BossRooms()) {
        const Game::BossRoomDef &br = entry.second;
        RoomGen::Options o;
        o.randomRoom = false;
        o.roomWidth = br.width;
        o.roomHeight = br.height;
        o.wallLevel = 2;
        o.obstacleLevel = 1;
        o.proceduralObstacles = false;
        for (const Game::BossRoomObject &obj : br.objects) {
            const Game::PrefabSkin *sk = gd.FindPrefabSkin(obj.prefab);
            if (sk != nullptr && sk->kind == "wall") {
                o.designSolidCells.emplace_back(obj.x, obj.y);
            }
        }
        const RoomGen rg(7, {1, 1, 1, 1}, o);
        RoomBlockerSet set;
        set.all = Room::FromRoomGen(rg, kCell, glm::vec2{0.0F, 0.0F}).Walls();
        AddDoorSeal(set, rg);
        for (const Game::BossRoomObject &obj : br.objects) {
            const Game::PrefabSkin *sk = gd.FindPrefabSkin(obj.prefab);
            if (sk == nullptr || sk->sprite.empty() || (sk->kind != "box" && sk->kind != "solid")) {
                continue;
            }
            const int code = rg.At(obj.x, obj.y);
            if (code == -1 || code == -2 || code == 11) {
                continue;
            }
            const glm::vec2 wpos = Room::CellToWorld(obj.x, obj.y, rg.Width(), rg.Height(), kCell);
            const auto data = FirstBlocker(sk->blockers, wpos, kCell);
            set.all.push_back(data.has_value() ? *data : Collider::MakeAABB(wpos, glm::vec2{kCell, kCell}));
        }
        const auto raw = FloorBlock::ConnectedFloorCells(rg);
        const auto blocks = [&set](glm::vec2 p, float r) { return set.Blocks(p, r); };
        const auto kept = FilterFreeCells(raw, rg.Width(), rg.Height(), kCell, glm::vec2{0.0F, 0.0F}, kHeroR, blocks);
        ASSERT_FALSE(kept.empty()) << entry.first;
        ++rooms;
        for (const auto &c : raw) {
            before += set.Blocks(CellPos(rg, c), kHeroR) ? 1 : 0;
        }
        for (const auto &c : kept) {
            after += set.Blocks(CellPos(rg, c), kHeroR) ? 1 : 0;
        }
        middlePickBefore += set.Blocks(CellPos(rg, raw[raw.size() / 2]), kHeroR) ? 1 : 0;
        middlePickAfter += set.Blocks(CellPos(rg, kept[kept.size() / 2]), kHeroR) ? 1 : 0;
    }
    std::printf("[RoomBlockersTest] boss rooms: %ld rooms, floor cells blocked before %ld after %ld; "
                "the boss/start 'middle cell' pick blocked before %ld after %ld\n",
                rooms, before, after, middlePickBefore, middlePickAfter);
    EXPECT_EQ(rooms, 14L);
    EXPECT_GT(before, 0L);
    EXPECT_EQ(after, 0L);
    EXPECT_EQ(middlePickAfter, 0L);
}

TEST(RoomBlockersTest, FilterFreeCellsKeepsOrderAndRejectsBlockedCentres) {
    const std::vector<std::pair<int, int>> cells{{1, 1}, {2, 1}, {3, 1}, {4, 1}};
    // 5x3 room centred on the origin: cell (x,y) -> ((x-2)*32, (y-1)*32)
    const Collider block = Collider::MakeAABB({32.0F, 0.0F}, {32.0F, 32.0F}); // exactly cell (3,1)
    const auto kept = FilterFreeCells(cells, 5, 3, kCell, glm::vec2{0.0F, 0.0F}, kHeroR,
                                      [&block](glm::vec2 p, float r) { return CircleHits(block, p, r); });
    // (3,1) is blocked, and its neighbours (2,1) / (4,1) are 32 px away: edge gap 32-16-12.8 = 3.2 -> free.
    EXPECT_EQ(kept, (std::vector<std::pair<int, int>>{{1, 1}, {2, 1}, {4, 1}}));
}

TEST(RoomBlockersTest, ResolveSpawnPointKeepsAFreeWantAndPicksTheNearestFreePointOtherwise) {
    const Collider block = Collider::MakeAABB({0.0F, 0.0F}, {32.0F, 32.0F});
    const auto blocks = [&block](glm::vec2 p, float r) { return CircleHits(block, p, r); };
    const std::vector<glm::vec2> pts{{0.0F, 0.0F}, {64.0F, 0.0F}, {-96.0F, 0.0F}, {0.0F, 100.0F}};
    EXPECT_EQ(ResolveSpawnPoint(pts, {64.0F, 0.0F}, kHeroR, blocks), (glm::vec2{64.0F, 0.0F}));
    EXPECT_EQ(ResolveSpawnPoint(pts, {0.0F, 0.0F}, kHeroR, blocks), (glm::vec2{64.0F, 0.0F})) << "nearest free";
    // nothing free: the wanted point comes back (the scene then depenetrates it)
    const std::vector<glm::vec2> none{{0.0F, 0.0F}};
    EXPECT_EQ(ResolveSpawnPoint(none, {0.0F, 0.0F}, kHeroR, blocks), (glm::vec2{0.0F, 0.0F}));
}

// NOLINTEND(readability-magic-numbers)
