#ifndef GAME_RGROOMX_HPP
#define GAME_RGROOMX_HPP

#include "data/RGRandom.hpp"

namespace Game {

/**
 * @class RGRoomX
 * @brief Faithful port of the recoverable, owner-independent slice of Soul
 *        Knight 1.7.10's @c RGRoomX: the room-lifecycle STATE MACHINE.
 *
 * The procedural grid build (@c SetUpRoom / @c CreateAisle / @c CreateFloor /
 * @c CreateWall / @c CreateObstacle / @c IsWallIntersect and the
 * @c SetRGRandomSeed size/level rolls) is already ported, determinism-locked,
 * in @c world/RoomGen. This class deliberately ports ONLY the genuinely-new
 * recoverable logic that @c RoomGen does not cover: the door open/close state
 * field and the @c ClearRoom gate that decides whether a reward is granted and
 * when the doors open.
 *
 * @par What is owner-side and intentionally NOT modelled here
 *  - The four @c RGDoor objects (@c +0x5c/0x60/0x64/0x68): @c RGRoomX__OpenDoor /
 *    @c CloseDoor / @c ChangeDoorsColor only forward to @c RGDoor__OpenDoor /
 *    @c RGDoor__CloseDoor / @c RGDoor__ChangeDoorColor (network-authoritative
 *    door entities). We model ONLY the @c door_open boolean (@c +0x6c) those
 *    methods write, not the door entities themselves.
 *  - @c RGMusicManager__PlayEffect(4) (the clear sting) -- owner audio.
 *  - @c GetRoomReward -> @c GetComponent<EnemyMaker> (truncated in the decomp;
 *    a component fetch with no recoverable body) -- owner.
 *  - @c Awake / @c LoadRoom / @c ClientReady / @c GetMinimapSprite: all bottom
 *    out in @c get_transform / @c GetComponent / @c Instantiate /
 *    @c NetControllerManager singleton access -- owner, no recoverable scalar
 *    or decision math beyond what is captured below.
 *
 * @par RNG
 * Every method modelled here is ZERO-DRAW: the room lifecycle never touches the
 * per-room @c RGRandom stream. The only draws in @c RGRoomX live in the grid
 * build, which is @c RoomGen's job. Tests assert the stream stays non-advancing.
 */
class RGRoomX {
public:
    /**
     * @enum Process
     * @brief Room lifecycle state (the @c process field @c +0x10).
     *
     * Values are the literal integers the decomp writes to @c process: @c 0 is
     * the initial/uncleared state, @c 1 is the active/locked combat state (set
     * by @c StartRoom, which is owner-driven and not modelled here), and @c 2 is
     * the cleared state (set by @c ClearRoom and by the type-0 fast path).
     */
    enum class Process : int {
        Uncleared = 0, ///< process == 0 (initial / before StartRoom).
        Active = 1,    ///< process == 1 (locked / in combat).
        Cleared = 2    ///< process == 2 (room cleared; set by ClearRoom).
    };

    /// Reward-room type: room_type == 1 grants a reward on clear (see ClearRoom).
    static constexpr int kRoomTypeReward = 1;

    /// Construct an uncleared, doors-closed room of the given type.
    /// @param roomType The @c room_type field (@c +0x1c); only the @c ==1 reward
    ///                 gate is decision-relevant to this state machine.
    explicit RGRoomX(int roomType = 0) : m_RoomType(roomType) {}

    /// Seed the per-room stream (parity helper; the lifecycle draws nothing).
    void SetSeed(int seed) { m_Rng.SetRandomSeed(seed); }

    /// @return The room_type field (@c +0x1c).
    int RoomType() const { return m_RoomType; }
    /// @return The current lifecycle state (@c process @c +0x10).
    Process State() const { return m_Process; }
    /// @return door_open (@c +0x6c): true after OpenDoor/ClearRoom, false after
    ///         CloseDoor. The default is false (doors closed).
    bool DoorOpen() const { return m_DoorOpen; }
    /// @return true exactly when ClearRoom decided to grant a reward (room_type
    ///         == 1). Lets a caller drive the owner-side GetRoomReward hook.
    bool RewardGranted() const { return m_RewardGranted; }

    /**
     * @brief Close all four doors (the @c door_open state transition only).
     *
     * Faithful to @c RGRoomX__CloseDoor: after forwarding to the four
     * @c RGDoor__CloseDoor calls (owner), the method writes @c door_open = 0.
     * We model only that state write; the door entities are owner-side.
     */
    void CloseDoor();

    /**
     * @brief Open all four doors (the @c door_open state transition only).
     *
     * Faithful to @c RGRoomX__OpenDoor: after the four @c RGDoor__OpenDoor calls
     * (owner), the method writes @c door_open = 1.
     */
    void OpenDoor();

    /**
     * @brief Clear the room: mark cleared, gate the reward, open the doors.
     *
     * Faithful to @c RGRoomX__ClearRoom. The recoverable decision/state logic:
     *   1. @c process = 2 (Cleared).
     *   2. (owner) @c RGMusicManager.PlayEffect(4) clear sting.
     *   3. if @c room_type == 1: grant the reward (owner @c GetRoomReward); we
     *      record the decision in @ref RewardGranted.
     *   4. @c OpenDoor() -> @c door_open = 1.
     *
     * Zero RNG draws.
     */
    void ClearRoom();

private:
    RGRandom m_Rng;                          ///< rg_random @ +0x0c (lifecycle draws nothing).
    int m_RoomType = 0;                      ///< room_type @ +0x1c.
    Process m_Process = Process::Uncleared;  ///< process @ +0x10.
    bool m_DoorOpen = false;                 ///< door_open @ +0x6c.
    bool m_RewardGranted = false;            ///< Records the ClearRoom room_type==1 gate.
};

} // namespace Game

#endif /* GAME_RGROOMX_HPP */
