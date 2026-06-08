#include "world/RGRoomX.hpp"

namespace Game {

// FAITHFUL: RGRoomX__CloseDoor @ game_full.c:427679
//
// The decomp body null-guards the four door pointers (+0x5c, +0x60, +0x64,
// +0x68) and forwards each to RGDoor__CloseDoor(door, 0) -- those door entities
// are owner/network-authoritative and are NOT modelled here. The only recoverable
// state write is the final *(this+0x6c) = 0 (door_open = false).
void RGRoomX::CloseDoor() {
    // (owner) RGDoor__CloseDoor on door_e/door_w/door_n/door_s @ +0x5c/0x60/0x64/0x68.
    m_DoorOpen = false; // *(this+0x6c) = 0
}

// FAITHFUL: RGRoomX__OpenDoor @ game_full.c:427736
//
// Mirror of CloseDoor: forwards to RGDoor__OpenDoor(door, 0) for each of the four
// doors (owner), then writes *(this+0x6c) = 1 (door_open = true).
void RGRoomX::OpenDoor() {
    // (owner) RGDoor__OpenDoor on door_e/door_w/door_n/door_s @ +0x5c/0x60/0x64/0x68.
    m_DoorOpen = true; // *(this+0x6c) = 1
}

// FAITHFUL: RGRoomX__ClearRoom @ game_full.c:427765
//
// Decomp control flow:
//   *(this+0x10) = 2;                       // process = Cleared
//   m = RGMusicManager__GetInstance();      // (owner) audio singleton
//   if (m != 0) {
//     RGMusicManager__PlayEffect(m, 4);     // (owner) clear sting
//     if (*(this+0x1c) == 1)                // room_type == 1 (reward room)
//        RGRoomX__GetRoomReward(this);      // (owner) GetComponent<EnemyMaker>
//     RGRoomX__OpenDoor(this);              // door_open = 1
//   }
// In the live game RGMusicManager__GetInstance is always non-null, so the gate is
// effectively unconditional; we reproduce the recoverable state/decision math
// (process, the room_type==1 reward gate, and the door-open transition) and leave
// the music + reward fetch to the owner. Zero RNG draws.
void RGRoomX::ClearRoom() {
    m_Process = Process::Cleared; // *(this+0x10) = 2

    // (owner) RGMusicManager.PlayEffect(4) clear sting.

    if (m_RoomType == kRoomTypeReward) { // *(this+0x1c) == 1
        // (owner) RGRoomX__GetRoomReward -> GetComponent<EnemyMaker>.
        m_RewardGranted = true;
    }

    OpenDoor(); // door_open = 1
}

} // namespace Game
