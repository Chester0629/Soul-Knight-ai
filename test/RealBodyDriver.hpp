#ifndef SK_TEST_REAL_BODY_DRIVER_HPP
#define SK_TEST_REAL_BODY_DRIVER_HPP

// =============================================================================
// RealBodyDriver -- combat-capable, real-collision headless TEST driver.
//
// VERIFICATION INFRASTRUCTURE, NOT GAME LOGIC. It lives entirely test-side, is
// never linked into the game binary, and changes no game behaviour (the game's
// non-driver path is byte-untouched). It is the generalisation of
// DoorTransitionTest's "real radius-16 body walked frame-by-frame through real
// BlocksAny collision + ASSERT body position" -- now with the REAL deterministic
// Simulation wired in so combat (aim/fire/kill) and the full clear-flow can be
// driven and asserted.
//
// WHY IT EXISTS: headless SK_AUTOWALK only LOGs "entered room N / locked" from
// Room::ContainsPoint -- a THRESHOLD rect-membership signal that flips the instant
// the body's centre touches the room rect (the door-cell outer face). That is a
// FALSE POSITIVE for "the body is in the room interior" (the air-wall bug hid in
// exactly that gap and was only caught by hand). This driver replaces that weak
// signal: every "arrived / entered / cleared / reached" assertion is made on the
// body's ACTUAL position via the SAME production collision (Room walls + corridor
// flanks + dynamic door seals) + Room::ContainsPointInset (deep-interior), never
// on the ContainsPoint threshold.
//
// It reuses the production leaf components verbatim (Room, FloorBlock, RoomGen,
// RGRoomX, Util::Collider/Overlap, Sim::Simulation, GameData) and mirrors the
// GameScene::Update orchestration (axis-separated move + lock-arm gate with the
// air-wall inset + clear gate). It does NOT refactor or touch game code.
//
// Determinism: this is test-side. The sim is driven exactly as GameScene drives
// it (same WorldInputs); the driver never perturbs the sim's RNG order, so the
// golden / combat-hash suites are unaffected (they don't include this file).
// =============================================================================

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Util/Collider.hpp"

#include "combat/CombatStats.hpp"
#include "data/GameData.hpp"
#include "sim/Simulation.hpp"
#include "sim/WorldCollision.hpp"
#include "sim/WorldInputs.hpp"
#include "world/FloorBlock.hpp"
#include "world/RGRoomX.hpp"
#include "world/Room.hpp"
#include "world/RoomGen.hpp"

namespace sktest {

class RealBodyDriver : public Game::Sim::WorldCollision {
public:
    static constexpr float kCellPx = 32.0F;
    static constexpr float kRadius = 16.0F; // kPlayerRadius
    static constexpr float kPitch =
        static_cast<float>(Game::FloorBlock::kBlock) * kCellPx; // 41*32 = 1312
    // Mirror of GameScene::kLockArmInset: one door cell + body radius + 1px to
    // clear the inclusive circle-vs-AABB tangency. The door-lock arms only once
    // the body's centre is this deep, so the seal closes BEHIND it (air-wall fix).
    static constexpr float kLockArmInset = kCellPx + kRadius + 1.0F; // 49px
    static constexpr float kAutoWalkSpeed = 300.0F;                  // px/s
    static constexpr float kStepMs = 20.0F; // one fixed sim step per frame
    static constexpr float kPickupRange = 28.0F; // GameScene pickup proximity (kPickupRange)
    static constexpr float kSkillDashPx = 80.0F; // (B1-P4a) C02 dash impulse distance (owner-applied)

    explicit RealBodyDriver(int seed) : m_Sim(seed, this) {}

    // --- floor build ---------------------------------------------------------
    /// Add a clean (obstacle-free) square room of @p size cells with door @p
    /// entrance flags {E,-y,W,+y}, at world @p origin. Registers its walls, the
    /// corridor flank walls for each open door, the full door-seal cell set, and
    /// an RGRoomX (doors open until StartRoom). Returns the room id.
    int AddRoom(glm::vec2 origin, int size, std::array<int, 4> entrance,
                int roomType = Game::RGRoomX::kRoomTypeReward) {
        Game::RoomGen::Options o;
        o.randomRoom = false;
        o.roomWidth = size;
        o.roomHeight = size;
        o.wallLevel = 0;     // clean interior: CreateObstacle places nothing
        o.obstacleLevel = 0; // -> only perimeter walls + carved door bands exist
        const Game::RoomGen rg(4242, entrance, o);

        RoomRec rec{Game::Room::FromRoomGen(rg, kCellPx, origin),
                    origin,
                    {},
                    {},
                    Game::RGRoomX(roomType),
                    size};
        // Corridor flank walls (mirror GameScene addCorridorWall) per open door.
        for (int dir = 0; dir < 4; ++dir) {
            if (entrance[static_cast<std::size_t>(dir)] != 1) {
                continue;
            }
            AddFlanks(rec.flanks, dir, rg, origin);
        }
        // The full door opening (perimeter aisle band + flank door cells).
        for (const auto &c : Game::FloorBlock::DoorSealCells(rg)) {
            rec.doorSeal.push_back(Util::Collider::MakeAABB(
                Game::Room::CellToWorld(c.first, c.second, rg.Width(), rg.Height(),
                                        kCellPx, origin),
                glm::vec2{kCellPx, kCellPx}));
        }
        rec.life.OpenDoor(); // faithful: doors open until StartRoom
        m_Rooms.push_back(std::move(rec));
        return static_cast<int>(m_Rooms.size()) - 1;
    }

    /// A standalone rectangular wall obstacle (for navigate-around-obstacle tests).
    void AddWall(glm::vec2 center, glm::vec2 size) {
        m_Walls.push_back(Util::Collider::MakeAABB(center, size));
    }

    void AddEnemy(int roomId, const Game::EnemyDef &def, glm::vec2 worldPos,
                  int seed) {
        m_Sim.AddEnemy(def, worldPos, roomId, seed);
    }
    void AddBoss(int roomId, glm::vec2 worldPos, int maxHp, int seed,
                 const std::string &bossId) {
        m_Sim.SetBoss(0.5F, worldPos, maxHp, roomId, seed, bossId); // brisk cadence for tests
    }
    void EquipPlayer(const Game::WeaponDef &def, int seed) {
        m_Sim.EquipWeapon(def, def.id.empty() ? "Gun001" : def.id, seed);
        m_EnergyCost = Game::WeaponEnergyCost(def);
    }
    /// (B1-P4a) Dispatch the player skill brain by character id. C01 mirror / C02 dash
    /// are faithful; c03..c13 are gate+cooldown-only stubs.
    void EquipSkill(const std::string &charId, float skillCd, float inSkillTime) {
        m_Sim.SetPlayerSkill(charId, skillCd, inSkillTime);
        m_SkillEquipped = true;
    }
    /// Hold/release the skill button for subsequent Frame()s.
    void SetSkillHeld(bool held) { m_SkillHeld = held; }
    /// Drop a weapon pickup at @p pos. Walking within kPickupRange auto-equips it
    /// (mirrors GameScene's proximity pickup -> EquipWeapon, the real walk-over chain).
    /// The body must navigate to it under real collision -- nothing equips until then.
    void AddWeaponPickup(glm::vec2 pos, const Game::WeaponDef &def) {
        m_Pickups.push_back(PickupRec{pos, def, false});
    }
    bool PickupTaken(std::size_t idx) const { return m_Pickups[idx].taken; }
    const std::string &CurrentWeaponId() const { return m_CurrentWeaponId; }
    /// Place the body and seed combat vitals (high energy so firing never starves).
    void SetPlayer(glm::vec2 pos) {
        m_Pos = pos;
        m_Player.hp = m_Player.maxHp = 100;
        m_Player.energy = m_Player.maxEnergy = 9999;
    }

    // --- WorldCollision (the SAME query the sim uses for enemy/bullet walls and
    //     the driver uses for the player body) ---------------------------------
    bool Blocks(glm::vec2 pos, float radius) const override {
        const Util::Collider circle = Util::Collider::MakeCircle(pos, radius);
        for (const Util::Collider &w : m_Walls) {
            if (Util::Overlap(w, circle)) {
                return true;
            }
        }
        for (const RoomRec &r : m_Rooms) {
            for (const Util::Collider &w : r.room.Walls()) {
                if (Util::Overlap(w, circle)) {
                    return true;
                }
            }
            for (const Util::Collider &f : r.flanks) {
                if (Util::Overlap(f, circle)) {
                    return true;
                }
            }
            if (!r.life.DoorOpen()) { // sealed only while locked (Active)
                for (const Util::Collider &d : r.doorSeal) {
                    if (Util::Overlap(d, circle)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // --- one frame (mirrors GameScene::Update: move, drive sim, lock gate) ----
    void Frame(glm::vec2 moveTarget, bool moving, bool firing,
               glm::vec2 aimTarget) {
        const glm::vec2 before = m_Pos;
        glm::vec2 after = before;
        if (moving) {
            const glm::vec2 d = moveTarget - before;
            const float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len > 1.0F) {
                after = before + (d / len) * (kAutoWalkSpeed * kStepMs / 1000.0F);
            }
        }
        glm::vec2 resolved = before; // axis-separated slide vs real collision
        if (!Blocks(glm::vec2{after.x, before.y}, kRadius)) {
            resolved.x = after.x;
        }
        if (!Blocks(glm::vec2{resolved.x, after.y}, kRadius)) {
            resolved.y = after.y;
        }
        m_Pos = resolved;

        // Proximity pickup -> equip (mirrors GameScene pickup-equip; the real walk-over).
        for (PickupRec &pk : m_Pickups) {
            if (!pk.taken && glm::distance(m_Pos, pk.pos) <= kPickupRange) {
                EquipPlayer(pk.def, 1000 + m_EquipSeq++);
                m_CurrentWeaponId = pk.def.id;
                pk.taken = true;
            }
        }

        const int prid = PlayerRoomId(); // ContainsPoint membership -> sim WAKE input

        Game::Sim::WorldInputs in;
        in.playerPos = m_Pos;
        in.playerRoomId = prid;
        in.playerAlive = !m_Player.IsDead();
        if (firing && m_Player.energy >= m_EnergyCost) {
            const glm::vec2 a = aimTarget - m_Pos;
            const float al = std::sqrt(a.x * a.x + a.y * a.y);
            in.aimDir = al > 0.0F ? a / al : glm::vec2{1.0F, 0.0F};
            in.firing = true;
        }
        in.skill = m_SkillHeld; // (B1-P4a) skill button -> sim TickSkill activation
        m_Sim.SetPlayerStats(m_Player);
        m_Sim.Advance(kStepMs, in);
        // (B1-P4a) owner-side: apply a C02 dash impulse the sim signalled (skill_dash
        // event). The sim's player position is an INPUT, so the dash is applied here
        // (the GameScene/owner's job), collision-checked like normal movement.
        if (m_SkillEquipped) {
            for (const Game::Sim::SimEvent &ev : m_Sim.DrainEvents()) {
                if (ev.name != "skill_dash" || ev.entityId != Game::Sim::Simulation::kPlayerViewId) {
                    continue;
                }
                const glm::vec2 ad = aimTarget - m_Pos;
                const float adl = std::sqrt(ad.x * ad.x + ad.y * ad.y);
                const glm::vec2 dir = adl > 0.0F ? ad / adl : glm::vec2{1.0F, 0.0F};
                const glm::vec2 step = dir * kSkillDashPx;
                if (!Blocks(glm::vec2{m_Pos.x + step.x, m_Pos.y}, kRadius)) {
                    m_Pos.x += step.x;
                }
                if (!Blocks(glm::vec2{m_Pos.x, m_Pos.y + step.y}, kRadius)) {
                    m_Pos.y += step.y;
                }
            }
        }
        m_Player.hp = m_Sim.PlayerStats().hp;
        m_Player.armor = m_Sim.PlayerStats().armor;
        for (int s = 0; s < m_Sim.PlayerShotsLastAdvance(); ++s) {
            m_Player.SpendEnergy(m_EnergyCost);
        }
        m_Player.EnergyReloadTick(kStepMs / 1000.0F);

        ApplyLockGate(prid); // StartRoom (inset) + ClearRoom -- the GameScene gate
    }

    // --- high-level driving (real collision; capped) -------------------------
    /// Steer the body straight at @p target (greedy + wall-slide). Returns true
    /// once the centre is within @p tol of the target.
    bool NavigateTo(glm::vec2 target, float tol, int maxFrames) {
        for (int f = 0; f < maxFrames; ++f) {
            if (glm::distance(m_Pos, target) <= tol) {
                return true;
            }
            Frame(target, /*moving=*/true, /*firing=*/false, glm::vec2{0.0F});
        }
        return glm::distance(m_Pos, target) <= tol;
    }

    /// Follow @p waypoints in order (each a NavigateTo leg). Returns true iff the
    /// final waypoint is reached. This is the real "route around the geometry"
    /// path (corridors / obstacles) -- not a single blind straight line.
    bool NavigateWaypoints(const std::vector<glm::vec2> &waypoints, float tol,
                           int maxFramesPerLeg) {
        bool ok = true;
        for (const glm::vec2 &wp : waypoints) {
            ok = NavigateTo(wp, tol, maxFramesPerLeg);
            if (!ok) {
                break;
            }
        }
        return ok;
    }

    /// Move toward and fire at the room's live hostiles until all are dead or the
    /// frame cap is hit. Returns true iff the room has no live hostile left.
    bool ClearRoomCombat(int roomId, int maxFrames) {
        for (int f = 0; f < maxFrames && RoomHasLiveHostile(roomId); ++f) {
            const glm::vec2 tgt = NearestHostilePos(roomId);
            Frame(tgt, /*moving=*/true, /*firing=*/true, tgt);
        }
        return !RoomHasLiveHostile(roomId);
    }

    /// Like ClearRoomCombat but PULSES the trigger (releases 1 frame in 4) so
    /// edge-triggered guns (Burst rising-edge, Charge windup->release) re-fire
    /// instead of stalling under a continuous hold. Auto-fire guns are unaffected.
    bool ClearRoomCombatPulsed(int roomId, int maxFrames) {
        for (int f = 0; f < maxFrames && RoomHasLiveHostile(roomId); ++f) {
            const glm::vec2 tgt = NearestHostilePos(roomId);
            const bool fire = (f % 4 != 0); // release every 4th frame -> re-arm the edge
            Frame(tgt, /*moving=*/true, fire, tgt);
        }
        return !RoomHasLiveHostile(roomId);
    }

    // --- queries -------------------------------------------------------------
    glm::vec2 PlayerPos() const { return m_Pos; }
    glm::vec2 RoomCenter(int roomId) const { return m_Rooms[static_cast<std::size_t>(roomId)].room.Center(); }
    float RoomHalf(int roomId) const {
        return static_cast<float>(m_Rooms[static_cast<std::size_t>(roomId)].size) * kCellPx * 0.5F;
    }
    bool DoorOpen(int roomId) const { return m_Rooms[static_cast<std::size_t>(roomId)].life.DoorOpen(); }
    bool RewardGranted(int roomId) const {
        return m_Rooms[static_cast<std::size_t>(roomId)].life.RewardGranted();
    }
    /// Real "is the body deep in the room interior" -- the assertion that REPLACES
    /// the ContainsPoint threshold false-positive. inset defaults to >1 cell.
    bool BodyDeepInside(int roomId, float inset = kLockArmInset) const {
        return m_Rooms[static_cast<std::size_t>(roomId)].room.ContainsPointInset(m_Pos, inset);
    }
    /// ContainsPoint membership (the WEAK threshold signal) -- exposed only so
    /// tests can show it is NOT used as the entry proof; never assert "entered" on it.
    int PlayerRoomId() const {
        for (std::size_t i = 0; i < m_Rooms.size(); ++i) {
            if (m_Rooms[i].room.ContainsPoint(m_Pos)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    bool RoomHasLiveHostile(int roomId) const {
        for (const auto &ev : m_Sim.EnemyViews()) {
            if (ev.alive && ev.roomId == roomId) {
                return true;
            }
        }
        if (m_Sim.HasBoss()) {
            const auto bv = m_Sim.BossView();
            if (bv.alive && bv.roomId == roomId) {
                return true;
            }
        }
        return false;
    }
    int LiveEnemyCount(int roomId) const {
        int n = 0;
        for (const auto &ev : m_Sim.EnemyViews()) {
            if (ev.alive && ev.roomId == roomId) {
                ++n;
            }
        }
        return n;
    }
    bool BossAlive() const { return m_Sim.HasBoss() && m_Sim.BossView().alive; }
    std::size_t BulletCount() const { return m_Sim.Bullets().size(); }
    int EnemyBulletCount() const {
        int n = 0;
        for (const auto &b : m_Sim.Bullets()) {
            if (b.camp == 1) {
                ++n;
            }
        }
        return n;
    }
    /// Live player bullets (camp 0) -- the in-flight pattern this tick. Used to
    /// assert the REAL fan width / burst sub-shot count from the body-driven loop.
    int PlayerBulletCount() const {
        int n = 0;
        for (const auto &b : m_Sim.Bullets()) {
            if (b.camp == 0) {
                ++n;
            }
        }
        return n;
    }
    const Game::CombatStats &Player() const { return m_Player; }

private:
    struct RoomRec {
        Game::Room room;
        glm::vec2 origin{0.0F, 0.0F};
        std::vector<Util::Collider> flanks;
        std::vector<Util::Collider> doorSeal;
        Game::RGRoomX life;
        int size = 15;
    };
    struct PickupRec {
        glm::vec2 pos{0.0F, 0.0F};
        Game::WeaponDef def;
        bool taken = false;
    };

    static glm::vec2 BlockWorld(int bx, int by, glm::vec2 origin) {
        return Game::Room::CellToWorld(bx, by, Game::FloorBlock::kBlock,
                                       Game::FloorBlock::kBlock, kCellPx, origin);
    }
    static void AddFlanks(std::vector<Util::Collider> &out, int dir,
                          const Game::RoomGen &rg, glm::vec2 origin) {
        const Game::FloorBlock::Rect s = Game::FloorBlock::CorridorStrip(dir, rg);
        const bool horiz = (dir == Game::FloorBlock::DIR_EAST ||
                            dir == Game::FloorBlock::DIR_WEST);
        const glm::vec2 cell{kCellPx, kCellPx};
        if (horiz) {
            for (int bx = s.x0; bx <= s.x1; ++bx) {
                out.push_back(Util::Collider::MakeAABB(BlockWorld(bx, s.y0 - 1, origin), cell));
                out.push_back(Util::Collider::MakeAABB(BlockWorld(bx, s.y1 + 1, origin), cell));
            }
        } else {
            for (int by = s.y0; by <= s.y1; ++by) {
                out.push_back(Util::Collider::MakeAABB(BlockWorld(s.x0 - 1, by, origin), cell));
                out.push_back(Util::Collider::MakeAABB(BlockWorld(s.x1 + 1, by, origin), cell));
            }
        }
    }

    glm::vec2 NearestHostilePos(int roomId) const {
        glm::vec2 best = m_Pos;
        float bestD = -1.0F;
        const auto consider = [&](glm::vec2 p) {
            const float d = glm::distance(m_Pos, p);
            if (bestD < 0.0F || d < bestD) {
                bestD = d;
                best = p;
            }
        };
        for (const auto &ev : m_Sim.EnemyViews()) {
            if (ev.alive && ev.roomId == roomId) {
                consider(ev.pos);
            }
        }
        if (m_Sim.HasBoss()) {
            const auto bv = m_Sim.BossView();
            if (bv.alive && bv.roomId == roomId) {
                consider(bv.pos);
            }
        }
        return best;
    }

    // The GameScene clear-room gate (with the air-wall inset). StartRoom arms only
    // once the body is kLockArmInset deep; ClearRoom reopens when no live hostile.
    void ApplyLockGate(int prid) {
        if (prid >= 0 &&
            m_Rooms[static_cast<std::size_t>(prid)].life.State() ==
                Game::RGRoomX::Process::Uncleared &&
            m_Rooms[static_cast<std::size_t>(prid)].room.ContainsPointInset(
                m_Pos, kLockArmInset) &&
            RoomHasLiveHostile(prid)) {
            m_Rooms[static_cast<std::size_t>(prid)].life.StartRoom();
        }
        for (std::size_t i = 0; i < m_Rooms.size(); ++i) {
            if (m_Rooms[i].life.State() == Game::RGRoomX::Process::Active &&
                !RoomHasLiveHostile(static_cast<int>(i))) {
                m_Rooms[i].life.ClearRoom();
            }
        }
    }

    std::vector<RoomRec> m_Rooms;
    std::vector<Util::Collider> m_Walls; // standalone obstacles (tests)
    std::vector<PickupRec> m_Pickups;    // walk-over weapon pickups (tests)
    Game::Sim::Simulation m_Sim;
    glm::vec2 m_Pos{0.0F, 0.0F};
    Game::CombatStats m_Player{};
    int m_EnergyCost = 1;
    std::string m_CurrentWeaponId;
    int m_EquipSeq = 0;
    bool m_SkillEquipped = false; // (B1-P4a) a skill brain was dispatched.
    bool m_SkillHeld = false;     // (B1-P4a) skill button held this/next Frame.
};

} // namespace sktest

#endif // SK_TEST_REAL_BODY_DRIVER_HPP
