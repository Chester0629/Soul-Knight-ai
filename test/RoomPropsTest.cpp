#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <string>

#include <glm/glm.hpp>

#include "data/GameData.hpp"
#include "game/ChestAnim.hpp"
#include "game/DoorAnim.hpp"
#include "game/RenderLayout.hpp"
#include "game/WorldScale.hpp"
#include "entities/Chest.hpp"

// NOLINTBEGIN(readability-magic-numbers)

// P2.20: the door / chest clips, the prefab shadows and the room_props.json data, FAITHFUL @ rgprefab/room/
// room/door_n.prefab + rganim/door_open|door_close2.anim + door_2.controller, res/item/chest*.prefab +
// rganim/chest_open.anim, and the `shadow` child of every wall / obstacle prefab.
namespace {

const char *kResourceRoot = RESOURCE_DIR;
namespace fs = std::filesystem;

const Game::GameData &Data() {
    static Game::GameData gd = [] {
        Game::GameData g;
        g.LoadAll(kResourceRoot);
        return g;
    }();
    return gd;
}

} // namespace

// ---- doors (AC3) ------------------------------------------------------------------------------------------

TEST(RoomPropsTest, DoorPrefabAndClipsAreTheOriginals) {
    const Game::DoorPropsDef &d = Data().RoomProps().door;
    ASSERT_TRUE(d.present);
    EXPECT_EQ(d.base.sprite, "door02.png");
    EXPECT_EQ(d.openSprite, "door03");
    EXPECT_EQ(d.bar.sprite, "door01.png");
    EXPECT_FLOAT_EQ(d.bar.offsetY, 1.0F);
    EXPECT_EQ(d.bar.sortLayer, 6);
    EXPECT_EQ(d.bar.sortOrder, 3);
    EXPECT_EQ(d.base.sortLayer, 4);
    EXPECT_EQ(d.shadow.sprite, "shadow0.png");
    EXPECT_EQ(d.shadow.sortLayer, 3);
    EXPECT_EQ(d.shadow.sortOrder, 1);
    EXPECT_NEAR(d.shadow.color[3], 0.392157F, 1e-5F);
    EXPECT_FLOAT_EQ(d.open.stop, 0.2F);
    EXPECT_NEAR(d.close.stop, 0.16666667F, 1e-6F);
    EXPECT_TRUE(fs::exists(fs::path(kResourceRoot) / "sprites" / (d.openSprite + ".png")));
}

TEST(RoomPropsTest, OpenDoorShowsDoor03WithoutBarOrShadowAndClosedShowsDoor02WithBarAndShadow) {
    const Game::DoorPropsDef &d = Data().RoomProps().door;
    namespace DA = Game::DoorAnim;
    // settled open (the end of door_open): door03, no bar, no shadow
    const DA::Pose open = DA::Sample(d, /*opening=*/true, d.open.stop);
    EXPECT_TRUE(open.baseOpen);
    EXPECT_FALSE(open.barOn);
    EXPECT_FALSE(open.shadowOn);
    EXPECT_NEAR(open.barY, 0.1F, 1e-6F);
    // settled closed (the end of door_close2): door02, bar raised to y = 1, shadow on
    const DA::Pose closed = DA::Sample(d, /*opening=*/false, d.close.stop);
    EXPECT_FALSE(closed.baseOpen);
    EXPECT_TRUE(closed.barOn);
    EXPECT_TRUE(closed.shadowOn);
    EXPECT_NEAR(closed.barY, 1.0F, 1e-4F); // keys are stored to 6 digits
}

TEST(RoomPropsTest, DoorOpenClipTimeline) {
    const Game::DoorPropsDef &d = Data().RoomProps().door;
    namespace DA = Game::DoorAnim;
    // t = 0: still closed (the clip starts from the closed pose)
    DA::Pose p = DA::Sample(d, true, 0.0F);
    EXPECT_FALSE(p.baseOpen);
    EXPECT_TRUE(p.barOn);
    EXPECT_TRUE(p.shadowOn);
    EXPECT_NEAR(p.barY, 1.0F, 1e-6F);
    // halfway through the bar's descent (0.133 s): y = (1 + 0.1) / 2
    p = DA::Sample(d, true, 0.133333F / 2.0F);
    EXPECT_NEAR(p.barY, 0.55F, 1e-3F);
    EXPECT_TRUE(p.shadowOn);
    // just after 0.133 s: the shadow is off, the bar is still enabled, the base is still door02
    p = DA::Sample(d, true, 0.14F);
    EXPECT_FALSE(p.shadowOn);
    EXPECT_TRUE(p.barOn);
    EXPECT_FALSE(p.baseOpen);
    // 0.167 s: bar off and the base swaps to door03
    p = DA::Sample(d, true, 0.1667F);
    EXPECT_FALSE(p.barOn);
    EXPECT_TRUE(p.baseOpen);
    // clamped beyond the clip
    EXPECT_EQ(DA::Sample(d, true, 5.0F).baseOpen, DA::Sample(d, true, d.open.stop).baseOpen);
    EXPECT_EQ(DA::Sample(d, true, -1.0F).barOn, DA::Sample(d, true, 0.0F).barOn);
}

TEST(RoomPropsTest, DoorCloseClipTimeline) {
    const Game::DoorPropsDef &d = Data().RoomProps().door;
    namespace DA = Game::DoorAnim;
    DA::Pose p = DA::Sample(d, false, 0.0F); // the clip starts from the open pose
    EXPECT_TRUE(p.baseOpen);
    EXPECT_FALSE(p.barOn);
    EXPECT_FALSE(p.shadowOn);
    EXPECT_NEAR(p.barY, 0.1F, 1e-6F);
    p = DA::Sample(d, false, 0.0334F); // 0.033 s: everything switches on and the base returns to door02
    EXPECT_FALSE(p.baseOpen);
    EXPECT_TRUE(p.barOn);
    EXPECT_TRUE(p.shadowOn);
    EXPECT_NEAR(p.barY, 0.1F + 0.9F * (0.0334F / 0.166667F), 1e-3F);
}

// ---- shadows (AC: shadow0 under walls / doors / boxes) -----------------------------------------------------

TEST(RoomPropsTest, EveryWallPrefabOfEveryLevelCarriesShadow0At39PercentOnLayer3) {
    int walls = 0;
    for (const Game::LevelDef &lv : Data().Levels()) {
        ASSERT_GE(lv.walls.size(), 2u) << lv.id << ": wall_list needs [plain, decorated]";
        for (const Game::LevelWallDef &w : lv.walls) {
            ++walls;
            ASSERT_TRUE(w.shadow.present) << lv.id << " " << w.prefab;
            EXPECT_EQ(w.shadow.sprite, "shadow0.png") << w.prefab;
            EXPECT_NEAR(w.shadow.color[3], 0.392157F, 1e-5F) << w.prefab;
            EXPECT_NEAR(w.shadow.color[0], 1.0F, 1e-6F) << w.prefab; // plain alpha blending, no multiply tint
            EXPECT_EQ(w.shadow.sortLayer, 3) << w.prefab;
            EXPECT_FLOAT_EQ(w.shadow.pivotX, 0.5F);
            EXPECT_FLOAT_EQ(w.shadow.pivotY, 0.3F);
            EXPECT_TRUE(fs::exists(fs::path(kResourceRoot) / "sprites" / w.shadow.sprite));
        }
    }
    EXPECT_EQ(walls, 62);
}

TEST(RoomPropsTest, ObstacleShadowsFollowThePrefabs) {
    const Game::LevelDef *a1 = Data().FindLevel("A1");
    ASSERT_NE(a1, nullptr);
    ASSERT_GE(a1->obstacles.size(), 12u);
    EXPECT_FALSE(a1->obstacles[0].shadow.present) << "wall_tree1 has no shadow child";
    for (int i : {1, 2, 3, 4}) { // box02 / cask_red_2 / cask_green_2 / cask_blue_2: shadow0 @ 0.39
        const auto &o = a1->obstacles[static_cast<std::size_t>(i)];
        ASSERT_TRUE(o.shadow.present) << o.prefab;
        EXPECT_EQ(o.shadow.sprite, "shadow0.png") << o.prefab;
        EXPECT_NEAR(o.shadow.color[3], 0.392157F, 1e-5F) << o.prefab;
    }
    for (int i : {5, 6, 7}) { // sting01 / speed_up / speed_down: none
        EXPECT_FALSE(a1->obstacles[static_cast<std::size_t>(i)].shadow.present) << a1->obstacles[i].prefab;
    }
    // every present shadow in the table is a shadow* sprite on layer 3 with a file on disk (shadow0 / shadow1 at alpha < 1; the shadow3 ellipses carry their own alpha)
    int present = 0;
    for (const Game::LevelDef &lv : Data().Levels()) {
        for (const Game::LevelObstacleDef &o : lv.obstacles) {
            if (!o.shadow.present) {
                continue;
            }
            ++present;
            EXPECT_EQ(o.shadow.sprite.rfind("shadow", 0), 0u) << o.prefab;
            EXPECT_EQ(o.shadow.sortLayer, 3) << o.prefab;
            if (o.shadow.sprite == "shadow0.png" || o.shadow.sprite == "shadow1.png") {
                EXPECT_LT(o.shadow.color[3], 1.0F) << o.prefab; // the rectangular blocks are drawn at alpha 0.39
            }
            EXPECT_TRUE(fs::exists(fs::path(kResourceRoot) / "sprites" / o.shadow.sprite)) << o.prefab;
        }
    }
    EXPECT_GT(present, 100);
}

TEST(RoomPropsTest, ShadowNodeSitsUnderItsWallAtTheCellWithTheNodePivot) {
    namespace RL = Game::RenderLayout;
    const Game::LevelDef *a1 = Data().FindLevel("A1");
    ASSERT_NE(a1, nullptr);
    const Game::RigNodeDef &sh = a1->walls[0].shadow;
    const glm::vec2 cell{64.0F, 96.0F};
    const RL::RoomNodePlacement pl = RL::PlaceRoomNode(sh, {16.0F, 24.0F}, cell);
    const glm::vec2 anchor = RL::CellAnchor(cell);
    // pivot (0.5, 0.3): centre = anchor + (0.5 - 0.3) * 24 * kSpriteScale above the anchor
    EXPECT_FLOAT_EQ(pl.translation.x, anchor.x);
    EXPECT_NEAR(pl.translation.y, anchor.y + 0.2F * 24.0F * Game::Scale::kSpriteScale, 1e-4F);
    EXPECT_FLOAT_EQ(pl.scale.x, Game::Scale::kSpriteScale);
    // layer 3 (Shadow) draws under the wall base (layer 4) and over the floor (layer 2)
    EXPECT_LT(pl.z, RL::RoomSortZ(4, 0, anchor.y));
    EXPECT_GT(pl.z, RL::RoomSortZ(2, 0, anchor.y));
}

// ---- spike prefab on the non-blocking skins ----------------------------------------------------------------

// ---- chests (AC5) -------------------------------------------------------------------------------------------

TEST(RoomPropsTest, EveryChestKindResolvesToAFourLayerPrefabWithShadow1) {
    const auto &props = Data().RoomProps();
    ASSERT_GE(props.chests.size(), 4u);
    for (int tier : {0, 1, 2, 3}) {
        for (const auto kind : {Game::Chest::Kind::Small, Game::Chest::Kind::RoomReward, Game::Chest::Kind::Boss}) {
            const auto it = props.chests.find(Game::Chest::PrefabFor(tier, kind));
            ASSERT_NE(it, props.chests.end()) << Game::Chest::PrefabFor(tier, kind);
            const Game::ChestPropsDef &c = it->second;
            EXPECT_EQ(c.shadow.sprite, "shadow1.png");
            EXPECT_NEAR(c.shadow.color[3], 0.392157F, 1e-5F);
            EXPECT_FLOAT_EQ(c.shadow.scaleX, 1.2F);
            EXPECT_FLOAT_EQ(c.shadow.scaleY, 1.0F);
            EXPECT_EQ(c.shadow.sortLayer, 3);
            EXPECT_FLOAT_EQ(c.box05.offsetY, -0.45F);   // interior
            EXPECT_EQ(c.box05.sortLayer, 4);
            EXPECT_EQ(c.box05.sortOrder, 0);
            EXPECT_EQ(c.box06.sortLayer, 4);            // front panel
            EXPECT_EQ(c.box06.sortOrder, 2);
            EXPECT_FLOAT_EQ(c.box03.pivotX, 1.0F);      // left lid half
            EXPECT_FLOAT_EQ(c.box04.pivotX, 0.0F);      // right lid half
            EXPECT_EQ(c.box03.sortLayer, 6);
            EXPECT_EQ(c.box04.sortLayer, 6);
            EXPECT_EQ(c.box03.sortOrder, 1);
            for (const Game::RigNodeDef *n : {&c.box03, &c.box04, &c.box05, &c.box06, &c.shadow}) {
                EXPECT_TRUE(fs::exists(fs::path(kResourceRoot) / "sprites" / n->sprite)) << n->sprite;
            }
        }
    }
    // the four layers differ per tier exactly as the prefabs say
    EXPECT_EQ(props.chests.at("chest01").box06.sprite, "objects_common_3.png");
    EXPECT_EQ(props.chests.at("chest00").box06.sprite, "objects_common_6.png");
    EXPECT_EQ(props.chests.at("chest02").box06.sprite, "objects_common_13.png");
    EXPECT_EQ(props.chests.at("chest_room_reward").box06.sprite, "objects_common_9.png");
}

TEST(RoomPropsTest, ChestOpenClipMovesTheLidsApartAndLiftsTheInterior) {
    namespace CA = Game::ChestAnim;
    const auto &props = Data().RoomProps();
    const Game::ChestPropsDef &c = props.chests.at("chest01");
    const Game::ChestOpenClipDef &clip = props.chestOpen;
    EXPECT_FLOAT_EQ(clip.stop, 1.0F);
    const CA::Pose closed = CA::ClosedPose(c);
    EXPECT_EQ(closed.box03, glm::vec2(0.0F, 0.0F));
    EXPECT_EQ(closed.box04, glm::vec2(0.0F, 0.0F));
    EXPECT_EQ(closed.box05, glm::vec2(0.0F, -0.45F));
    CA::Pose p = CA::OpenPose(c, clip, 0.0F);
    EXPECT_EQ(p.box03, glm::vec2(0.0F, 0.0F));
    EXPECT_EQ(p.box05, glm::vec2(0.0F, -0.5F));
    p = CA::OpenPose(c, clip, 0.233333F / 2.0F); // halfway through the x slide
    EXPECT_NEAR(p.box03.x, -0.25F, 1e-3F);
    EXPECT_NEAR(p.box04.x, 0.25F, 1e-3F);
    p = CA::OpenPose(c, clip, 0.233333F);
    EXPECT_NEAR(p.box03.x, -0.5F, 1e-4F);
    EXPECT_NEAR(p.box04.x, 0.5F, 1e-4F);
    EXPECT_NEAR(p.box03.y, 0.0F, 1e-4F);
    p = CA::OpenPose(c, clip, 0.5F);
    EXPECT_NEAR(p.box03.y, 0.1F, 1e-4F);
    EXPECT_NEAR(p.box04.y, 0.1F, 1e-4F);
    EXPECT_NEAR(p.box05.y, 0.0F, 1e-4F);
    p = CA::OpenPose(c, clip, 9.0F); // clamped at the clip end
    EXPECT_NEAR(p.box03.x, -0.5F, 1e-6F);
    EXPECT_NEAR(p.box05.y, 0.0F, 1e-6F);
    // box05 rises monotonically
    float prev = -1.0F;
    for (float t = 0.0F; t <= 1.0F; t += 0.05F) {
        const float y = CA::OpenPose(c, clip, t).box05.y;
        EXPECT_GE(y, prev - 1e-6F);
        prev = y;
    }
}

TEST(RoomPropsTest, ChestLayerPlacementUsesTheNodePivotAtTheFeet) {
    namespace CA = Game::ChestAnim;
    const Game::ChestPropsDef &c = Data().RoomProps().chests.at("chest01");
    const glm::vec2 feet{200.0F, 300.0F};
    // right lid half: 10x20, pivot (0,0) at local (0.5, 0.1) -> the pivot point lands on feet + (16, 3.2) px
    const CA::NodePlacement r = CA::Place(c.box04, feet, {0.5F, 0.1F}, {10.0F, 20.0F});
    EXPECT_FLOAT_EQ(r.translation.x, 200.0F + 0.5F * Game::Scale::kCellPx);
    EXPECT_FLOAT_EQ(r.translation.y, 300.0F + 0.1F * Game::Scale::kCellPx);
    EXPECT_FLOAT_EQ(r.pivotPx.x, -5.0F);  // (0 - 0.5) * 10: the left edge of the rect
    EXPECT_FLOAT_EQ(r.pivotPx.y, -10.0F); // the bottom edge
    // left half pivots on its RIGHT edge
    const CA::NodePlacement l = CA::Place(c.box03, feet, {-0.5F, 0.0F}, {10.0F, 20.0F});
    EXPECT_FLOAT_EQ(l.pivotPx.x, 5.0F);
    // the shadow is 1.2 x wider than a cell sprite
    const CA::NodePlacement sh = CA::Place(c.shadow, feet, {0.0F, 0.0F}, {16.0F, 24.0F});
    EXPECT_FLOAT_EQ(sh.scale.x, Game::Scale::kSpriteScale * 1.2F);
    EXPECT_FLOAT_EQ(sh.scale.y, Game::Scale::kSpriteScale);
    EXPECT_FLOAT_EQ(sh.pivotPx.y, (0.3F - 0.5F) * 24.0F);
}

// NOLINTEND(readability-magic-numbers)
