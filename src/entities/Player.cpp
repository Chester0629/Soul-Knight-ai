#include "entities/Player.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "Util/Input.hpp"
#include "Util/Keycode.hpp"
#include "sim/SimConfig.hpp"

namespace Game {
namespace {
// The c01 character has a 9-frame walk/idle cycle: c01_0.png .. c01_8.png.
std::vector<std::string> C01Frames(const std::string &root) {
    std::vector<std::string> frames;
    frames.reserve(9);
    for (int i = 0; i < 9; ++i) {
        frames.push_back(root + "/sprites/c01_" + std::to_string(i) + ".png");
    }
    return frames;
}
// P3.0: characters.json `speed` is the RoleAttribute move speed in Unity units/s (c01 6.5), so
// the walk speed in px/s goes through the single data-speed factor (1 unit = 32 px). It was
// a private 30 px per unit literal before, a second, unsourced scale.
constexpr float kPixelsPerSpeedUnit = Sim::kDataSpeedToPxPerSec;
} // namespace

Player::Player(const CharacterDef &def, const std::string &resourceRoot,
               std::vector<std::string> frames, int frameIntervalMs)
    : m_Stats(CombatStats::FromCharacter(def)),
      m_Speed(def.speed * kPixelsPerSpeedUnit),
      // SPRITE MANIFEST: the selected character's real idle frames (sprites.json;
      // c01 idle is c01_4..7). The c01_0..8 placeholder only without a manifest.
      m_Anim(std::make_shared<Util::Animation>(
          frames.empty() ? C01Frames(resourceRoot) : frames, true,
          frameIntervalMs, true, 100)) {
    SetDrawable(m_Anim);
    SetZIndex(5.0F);
    m_Transform.translation = {0.0F, 0.0F};
    SyncSprite(); // kSpriteScale + bottom-centre pivot until the scene installs the data anchor
}

void Player::Update(float dtMs) {
    glm::vec2 dir{0.0F, 0.0F};
    if (Util::Input::IsKeyPressed(Util::Keycode::W)) {
        dir.y += 1.0F; // center-origin world: +y is up
    }
    if (Util::Input::IsKeyPressed(Util::Keycode::S)) {
        dir.y -= 1.0F;
    }
    if (Util::Input::IsKeyPressed(Util::Keycode::A)) {
        dir.x -= 1.0F;
    }
    if (Util::Input::IsKeyPressed(Util::Keycode::D)) {
        dir.x += 1.0F;
    }

    if (dir.x != 0.0F || dir.y != 0.0F) {
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        dir /= len;
        m_Transform.translation += dir * (m_Speed * dtMs / 1000.0F);
    }
    SyncSprite();
}
} // namespace Game
