#ifndef GAME_SIM_SIMEVENT_HPP
#define GAME_SIM_SIMEVENT_HPP

#include <cstdint>
#include <string>

namespace Game::Sim {

enum class SimEventType { AnimTrigger, Sfx };

/// A render/audio cue a controller emits; GameScene drains these each frame.
/// Audio is out of scope this cycle -- the shell may ignore Sfx events.
struct SimEvent {
    SimEventType type = SimEventType::AnimTrigger;
    std::uint32_t entityId = 0;
    std::string name; ///< anim-trigger name or sfx id.
};

} // namespace Game::Sim

#endif /* GAME_SIM_SIMEVENT_HPP */
