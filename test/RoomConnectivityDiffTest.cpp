#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"
#include "data/GameData.hpp"
#include "world/FloorBlock.hpp"
#include "world/Room.hpp"
#include "world/RoomBlockers.hpp"
#include "world/RoomGen.hpp"

using Game::DesignObstacleCollider;
using Game::FirstBlocker;
using Game::GameData;
using Game::Room;
using Game::RoomGen;

// NOLINTBEGIN(readability-magic-numbers)

// P2.18 / R37-R38: DIFFERENTIAL room-connectivity regression. The new blocker rules (prefab collider
// shapes, obj_index 8/9/10 colliding, 1.5-wide props, 2x2 statues ...) must not LOSE any door pair
// that the OLD rules (32x32 cells for boxes / braziers, 8/9/10 non-blocking) had connected.
//
// Method (from the independent verifier's lattice check): rasterise every blocker on a 4 px lattice
// grown by a 16 px body (the old kPlayerRadius, deliberately larger than the 12.8 px hero), then for
// each of the four doors (W/E/S/N) take its EFFECTIVE ENTRANCE = the door-mouth lattice points that
// are not themselves blocked (the outermost mouth cells are clipped by the border walls for a 16 px
// body in every rule set, so only the unblocked ones count), and flood. Door pair (a, b) is
// CONNECTED when some effective mouth point of a and some of b lie in one component. A door whose
// entrance is fully blocked has no connected pairs, i.e. it is LOST. The test compares PAIR BY PAIR:
// a pair connected under the old rules must still be connected under the new ones, so a rule that
// seals a single door while the others stay mutually connected fails, and so does an
// already-broken room that loses one more pair. All doors are open: the question is purely "do the
// obstacles seal part of the room".
//
// LIMITATION (documented, not hidden): destructible boxes are treated as PERMANENT walls here. In
// play a bullet breaks them, so rooms whose boxes alone seal a door pair under BOTH rule sets
// (r1_50 / r1_52 / r1_55 / r1_74 / r1_75 / r1_78) are still passable by shooting; they are reported,
// not failed. The test asserts only "the new rules lose no pair the old rules had".
//
// Coverage: by default one level per DISTINCT obstacle-collider signature (the 30 levels that have an
// obstacle table collapse to 7: connectivity depends only on shapes) x 108 design rooms x 3 seeds +
// 14 boss rooms x 3 seeds; SK_CONN_FULL=1 runs all 30 levels (30 x 108 x 3 = 9720 design configs
// + 42 boss configs). G1 has no obstacle table (the scene falls back to the old rules there), so it
// is identical to the old rules by construction and is not iterated.
namespace {
const char *kResourceRoot = RESOURCE_DIR;
constexpr float kCell = 32.0F;
constexpr float kStep = 4.0F;
constexpr float kBodyR = 16.0F;

enum Side { kWest = 0, kEast = 1, kSouth = 2, kNorth = 3 };

struct Res {
    std::array<bool, 4> effective{};          // door has at least one unblocked mouth point
    std::array<std::array<bool, 4>, 4> pair{}; // door pair connected (symmetric)
    int effectiveDoors = 0;
};

// A room is "sealed" (diagnostic only) when its effective doors are not all mutually connected.
bool Sealed(const Res &r) {
    for (int a = 0; a < 4; ++a) {
        for (int b = a + 1; b < 4; ++b) {
            if (r.effective[static_cast<std::size_t>(a)] && r.effective[static_cast<std::size_t>(b)] &&
                !r.pair[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)]) {
                return true;
            }
        }
    }
    return false;
}

// Door pairs connected in `oldR` that are NOT connected in `newR`.
int LostPairs(const Res &oldR, const Res &newR) {
    int lost = 0;
    for (std::size_t a = 0; a < 4; ++a) {
        for (std::size_t b = a + 1; b < 4; ++b) {
            if (oldR.pair[a][b] && !newR.pair[a][b]) {
                ++lost;
            }
        }
    }
    return lost;
}

// Doors whose entrance was usable under the old rules and is fully blocked under the new ones.
int LostDoors(const Res &oldR, const Res &newR) {
    int lost = 0;
    for (std::size_t a = 0; a < 4; ++a) {
        if (oldR.effective[a] && !newR.effective[a]) {
            ++lost;
        }
    }
    return lost;
}

Res Analyse(const RoomGen &rg, const std::vector<Util::Collider> &cols, float r) {
    const int w = rg.Width();
    const int h = rg.Height();
    const float hx = static_cast<float>(w) * kCell * 0.5F;
    const float hy = static_cast<float>(h) * kCell * 0.5F;
    const int nx = static_cast<int>(2 * hx / kStep) + 1;
    const int ny = static_cast<int>(2 * hy / kStep) + 1;
    std::vector<char> blocked(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), 0);
    const auto at = [ny](int i, int j) {
        return static_cast<std::size_t>(i) * static_cast<std::size_t>(ny) + static_cast<std::size_t>(j);
    };
    for (const Util::Collider &c : cols) {
        const glm::vec2 mn = c.Min() - glm::vec2(r + kStep);
        const glm::vec2 mx = c.Max() + glm::vec2(r + kStep);
        const int i0 = std::max(0, static_cast<int>(std::floor((mn.x + hx) / kStep)));
        const int i1 = std::min(nx - 1, static_cast<int>(std::ceil((mx.x + hx) / kStep)));
        const int j0 = std::max(0, static_cast<int>(std::floor((mn.y + hy) / kStep)));
        const int j1 = std::min(ny - 1, static_cast<int>(std::ceil((mx.y + hy) / kStep)));
        for (int i = i0; i <= i1; ++i) {
            for (int j = j0; j <= j1; ++j) {
                const glm::vec2 p(-hx + static_cast<float>(i) * kStep, -hy + static_cast<float>(j) * kStep);
                if (Util::Overlap(c, Util::Collider::MakeCircle(p, r))) {
                    blocked[at(i, j)] = 1;
                }
            }
        }
    }
    // Door mouths grouped by the side of the room they open on.
    std::array<std::vector<std::pair<int, int>>, 4> mouths;
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const int code = rg.At(x, y);
            if (!(code == -2 || code == 11)) {
                continue;
            }
            int side = -1;
            if (x == 0) {
                side = kWest;
            } else if (x == w - 1) {
                side = kEast;
            } else if (y == 0) {
                side = kSouth;
            } else if (y == h - 1) {
                side = kNorth;
            }
            if (side < 0) {
                continue;
            }
            const glm::vec2 p = Room::CellToWorld(x, y, w, h, kCell);
            const int i = std::clamp(static_cast<int>(std::lround((p.x + hx) / kStep)), 0, nx - 1);
            const int j = std::clamp(static_cast<int>(std::lround((p.y + hy) / kStep)), 0, ny - 1);
            mouths[static_cast<std::size_t>(side)].emplace_back(i, j);
        }
    }
    Res res;
    std::vector<int> comp(blocked.size(), -1);
    int comps = 0;
    std::array<std::set<int>, 4> doorComps;
    for (std::size_t s = 0; s < 4; ++s) {
        for (const auto &m : mouths[s]) {
            if (blocked[at(m.first, m.second)] != 0) {
                continue; // not part of the effective entrance
            }
            if (comp[at(m.first, m.second)] < 0) {
                const int id = comps++;
                std::vector<std::pair<int, int>> st{{m.first, m.second}};
                comp[at(m.first, m.second)] = id;
                while (!st.empty()) {
                    const auto cur = st.back();
                    st.pop_back();
                    const int dx[4] = {1, -1, 0, 0};
                    const int dy[4] = {0, 0, 1, -1};
                    for (int d = 0; d < 4; ++d) {
                        const int na = cur.first + dx[d];
                        const int nb = cur.second + dy[d];
                        if (na < 0 || nb < 0 || na >= nx || nb >= ny) {
                            continue;
                        }
                        const std::size_t k = at(na, nb);
                        if (blocked[k] != 0 || comp[k] >= 0) {
                            continue;
                        }
                        comp[k] = id;
                        st.emplace_back(na, nb);
                    }
                }
            }
            doorComps[s].insert(comp[at(m.first, m.second)]);
        }
        res.effective[s] = !doorComps[s].empty();
        res.effectiveDoors += res.effective[s] ? 1 : 0;
    }
    for (std::size_t a = 0; a < 4; ++a) {
        for (std::size_t b = 0; b < 4; ++b) {
            bool conn = false;
            for (int c : doorComps[a]) {
                conn = conn || doorComps[b].count(c) != 0;
            }
            res.pair[a][b] = conn;
        }
    }
    return res;
}

void SolidCells(const RoomGen &rg, std::vector<Util::Collider> &cols) {
    const int w = rg.Width();
    const int h = rg.Height();
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            if (Room::IsSolidCell(rg.At(x, y))) {
                cols.push_back(Util::Collider::MakeAABB(Room::CellToWorld(x, y, w, h, kCell), glm::vec2(kCell, kCell)));
            }
        }
    }
}

// Levels with identical obstacle collider shapes are equivalent for connectivity.
std::string Signature(const Game::LevelDef &lv) {
    std::string s;
    char buf[160];
    for (std::size_t i = 0; i < lv.obstacles.size(); ++i) {
        s += std::to_string(i) + ":";
        for (const Game::ColliderDef &c : lv.obstacles[i].blockers) {
            std::snprintf(buf, sizeof(buf), "%d,%.3f,%.3f,%.3f,%.3f,%.3f;", static_cast<int>(c.shape), c.radius, c.sizeX,
                          c.sizeY, c.offsetX, c.offsetY);
            s += buf;
        }
        s += "|";
    }
    return s;
}

RoomGen BareRoom(int size) {
    RoomGen::Options o;
    o.randomRoom = false;
    o.roomWidth = size;
    o.roomHeight = size;
    o.proceduralObstacles = false;
    return RoomGen(1, {1, 1, 1, 1}, o);
}
} // namespace

// ---------------------------------------------------------------------------------------------
// The comparison itself must be able to FAIL (R38): synthetic counterexamples on a bare 15x15 room.
// ---------------------------------------------------------------------------------------------
TEST(RoomConnectivityDiffTest, BareRoomHasAllFourDoorsMutuallyConnected) {
    const RoomGen rg = BareRoom(15);
    std::vector<Util::Collider> cols;
    SolidCells(rg, cols);
    const Res r = Analyse(rg, cols, kBodyR);
    EXPECT_EQ(r.effectiveDoors, 4);
    EXPECT_FALSE(Sealed(r));
    for (std::size_t a = 0; a < 4; ++a) {
        for (std::size_t b = 0; b < 4; ++b) {
            EXPECT_TRUE(r.pair[a][b]) << a << "," << b;
        }
    }
    EXPECT_EQ(LostPairs(r, r), 0);
}

TEST(RoomConnectivityDiffTest, DiffFailsWhenANewRuleFullySealsExactlyOneDoorWhileOthersStayConnected) {
    const RoomGen rg = BareRoom(15);
    std::vector<Util::Collider> oldCols;
    SolidCells(rg, oldCols);
    std::vector<Util::Collider> newCols = oldCols;
    // A blocker sitting ON the whole west door entrance (every west mouth point blocked): the other
    // three doors remain mutually connected, so a whole-room "all mouths in one component, blocked
    // mouths skipped" test would still pass -- the per-door-pair diff must not.
    newCols.push_back(Util::Collider::MakeAABB({-240.0F + 24.0F, 0.0F}, {96.0F, 480.0F}));
    const Res oldR = Analyse(rg, oldCols, kBodyR);
    const Res newR = Analyse(rg, newCols, kBodyR);
    ASSERT_EQ(oldR.effectiveDoors, 4);
    EXPECT_FALSE(newR.effective[kWest]) << "the west entrance is fully blocked";
    EXPECT_EQ(newR.effectiveDoors, 3);
    EXPECT_FALSE(Sealed(newR)) << "the remaining three doors are still mutually connected: the old whole-room check passes";
    EXPECT_EQ(LostDoors(oldR, newR), 1);
    EXPECT_EQ(LostPairs(oldR, newR), 3) << "west lost its pairs with east, south and north";
}

TEST(RoomConnectivityDiffTest, DiffFailsWhenAnAlreadyBrokenRoomLosesOneMorePair) {
    const RoomGen rg = BareRoom(15);
    std::vector<Util::Collider> oldCols;
    SolidCells(rg, oldCols);
    // OLD rules: a full-width wall below the north door isolates it (room already "broken").
    oldCols.push_back(Util::Collider::MakeAABB({0.0F, 160.0F}, {480.0F, 32.0F}));
    std::vector<Util::Collider> newCols = oldCols;
    // NEW rules additionally wall off the east side (below the north wall): east loses west + south.
    newCols.push_back(Util::Collider::MakeAABB({176.0F, 0.0F}, {32.0F, 480.0F}));
    const Res oldR = Analyse(rg, oldCols, kBodyR);
    const Res newR = Analyse(rg, newCols, kBodyR);
    ASSERT_TRUE(Sealed(oldR)) << "precondition: the room is already broken under the old rules";
    ASSERT_TRUE(oldR.pair[kWest][kEast]) << "precondition: west-east connected under the old rules";
    ASSERT_TRUE(oldR.pair[kEast][kSouth]);
    EXPECT_TRUE(Sealed(newR));
    EXPECT_GE(LostPairs(oldR, newR), 2) << "a whole-room boolean (broken -> broken) cannot see this";
    EXPECT_FALSE(newR.pair[kWest][kEast]);
}

// ---------------------------------------------------------------------------------------------
// The sweep over the real data.
// ---------------------------------------------------------------------------------------------
TEST(RoomConnectivityDiffTest, NewBlockerRulesNeverLoseADoorPairTheOldRulesHadConnected) {
    GameData gd;
    ASSERT_TRUE(gd.LoadAll(kResourceRoot));
    ASSERT_EQ(gd.DesignRooms().size(), 108u);
    const bool full = std::getenv("SK_CONN_FULL") != nullptr;
    std::vector<const Game::LevelDef *> levels;
    std::set<std::string> sigs;
    for (const Game::LevelDef &lv : gd.Levels()) {
        if (lv.obstacles.size() < 12) {
            continue; // G1 has no obstacle table: identical to the old rules
        }
        if (full || sigs.insert(Signature(lv)).second) {
            levels.push_back(&lv);
        }
    }
    const auto t0 = std::chrono::steady_clock::now();
    const std::array<int, 4> doors = {1, 1, 1, 1};
    long configs = 0;
    long oldPairs = 0; // door pairs connected under the old rules (the quantity that must not shrink)
    long lostPairs = 0;
    long lostDoors = 0;
    long sealedOld = 0;
    long sealedNew = 0;
    std::set<std::string> bothSealed;
    for (const Game::LevelDef *lv : levels) {
        for (const auto &kv : gd.DesignRooms()) {
            const Game::DesignRoomDef &def = kv.second;
            for (int seed : {1, 2, 3}) {
                RoomGen::Options o;
                o.randomRoom = false;
                o.roomWidth = def.width;
                o.roomHeight = def.height;
                o.proceduralObstacles = false;
                for (const auto &ob : def.obstacles) {
                    if (ob.objIndex == 0) {
                        o.designSolidCells.emplace_back(ob.x, ob.y);
                    }
                }
                const RoomGen rg(seed, doors, o);
                Res results[2];
                for (int mode = 0; mode < 2; ++mode) { // 0 = old rules (no level data), 1 = new rules
                    std::vector<Util::Collider> cols;
                    SolidCells(rg, cols);
                    for (const auto &ob : def.obstacles) {
                        if (ob.x < 0 || ob.x >= rg.Width() || ob.y < 0 || ob.y >= rg.Height() || ob.objIndex <= 0) {
                            continue;
                        }
                        const int code = rg.At(ob.x, ob.y);
                        if (code == -1 || code == -2 || code == 11) {
                            continue;
                        }
                        const auto c = DesignObstacleCollider(mode == 1 ? lv : nullptr, ob.objIndex,
                                                              Room::CellToWorld(ob.x, ob.y, rg.Width(), rg.Height(), kCell), kCell);
                        if (c.has_value()) {
                            cols.push_back(*c);
                        }
                    }
                    results[mode] = Analyse(rg, cols, kBodyR);
                }
                ++configs;
                for (std::size_t a = 0; a < 4; ++a) {
                    for (std::size_t b = a + 1; b < 4; ++b) {
                        oldPairs += results[0].pair[a][b] ? 1 : 0;
                    }
                }
                const int lp = LostPairs(results[0], results[1]);
                const int ld = LostDoors(results[0], results[1]);
                lostPairs += lp;
                lostDoors += ld;
                sealedOld += Sealed(results[0]) ? 1 : 0;
                sealedNew += Sealed(results[1]) ? 1 : 0;
                if (lp > 0 || ld > 0) {
                    ADD_FAILURE() << "NEW rules lose connectivity: design " << kv.first << " level " << lv->id << " seed " << seed
                                  << " lostPairs=" << lp << " lostDoors=" << ld;
                }
                if (Sealed(results[1])) {
                    bothSealed.insert(kv.first);
                }
            }
        }
    }
    // Boss rooms: same pair-wise differential (prefab blockers vs the old 32x32 cell for every box / solid).
    long bossConfigs = 0;
    long bossLost = 0;
    for (const auto &kv : gd.BossRooms()) {
        const Game::BossRoomDef &bd = kv.second;
        for (int seed : {1, 2, 3}) {
            RoomGen::Options o;
            o.randomRoom = false;
            o.roomWidth = bd.width;
            o.roomHeight = bd.height;
            o.proceduralObstacles = false;
            for (const auto &ob : bd.objects) {
                const Game::PrefabSkin *sk = gd.FindPrefabSkin(ob.prefab);
                if (sk != nullptr && sk->kind == "wall") {
                    o.designSolidCells.emplace_back(ob.x, ob.y);
                }
            }
            const RoomGen rg(seed, doors, o);
            Res results[2];
            for (int mode = 0; mode < 2; ++mode) {
                std::vector<Util::Collider> cols;
                SolidCells(rg, cols);
                for (const auto &ob : bd.objects) {
                    if (ob.x < 0 || ob.x >= rg.Width() || ob.y < 0 || ob.y >= rg.Height()) {
                        continue;
                    }
                    const int code = rg.At(ob.x, ob.y);
                    if (code == -1 || code == -2 || code == 11) {
                        continue;
                    }
                    const Game::PrefabSkin *sk = gd.FindPrefabSkin(ob.prefab);
                    if (sk == nullptr || sk->sprite.empty() || !(sk->kind == "box" || sk->kind == "solid")) {
                        continue;
                    }
                    const glm::vec2 wp = Room::CellToWorld(ob.x, ob.y, rg.Width(), rg.Height(), kCell);
                    const auto data = mode == 1 ? FirstBlocker(sk->blockers, wp, kCell) : std::nullopt;
                    cols.push_back(data.has_value() ? *data : Util::Collider::MakeAABB(wp, glm::vec2(kCell, kCell)));
                }
                results[mode] = Analyse(rg, cols, kBodyR);
            }
            ++bossConfigs;
            const int lp = LostPairs(results[0], results[1]) + LostDoors(results[0], results[1]);
            bossLost += lp;
            if (lp > 0) {
                ADD_FAILURE() << "NEW rules lose boss-room connectivity: " << kv.first << " seed " << seed;
            }
        }
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::string rooms;
    for (const std::string &r : bothSealed) {
        rooms += r + " ";
    }
    std::printf("[RoomConnectivityDiffTest] %s: %zu level signature(s) x 108 design rooms x 3 seeds = %ld configs "
                "(+%ld boss-room configs); door pairs connected under the old rules %ld; pairs lost under the new "
                "rules %ld, doors lost %ld (design) / %ld (boss); sealed rooms (configs) old %ld new %ld, rooms "
                "sealed under the new rules: %s; %.1f s\n",
                full ? "FULL (all 30 levels with an obstacle table)" : "default", levels.size(), configs, bossConfigs,
                oldPairs, lostPairs, lostDoors, bossLost, sealedOld, sealedNew, rooms.c_str(), secs);
    EXPECT_EQ(lostPairs, 0);
    EXPECT_EQ(lostDoors, 0);
    EXPECT_EQ(bossLost, 0);
    EXPECT_GT(oldPairs, 0);
    EXPECT_EQ(configs, static_cast<long>(levels.size()) * 108L * 3L);
    EXPECT_EQ(bossConfigs, 14L * 3L);
}

// NOLINTEND(readability-magic-numbers)
