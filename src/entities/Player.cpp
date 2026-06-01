#include "entities/Player.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "Util/Input.hpp"
#include "Util/Keycode.hpp"

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
constexpr float kPixelsPerSpeedUnit = 30.0F;
} // namespace

Player::Player(const CharacterDef &def, const std::string &resourceRoot)
    : m_Stats(CombatStats::FromCharacter(def)),
      m_Speed(def.speed * kPixelsPerSpeedUnit),
      m_Anim(std::make_shared<Util::Animation>(C01Frames(resourceRoot), true, 80,
                                               true, 100)) {
    SetDrawable(m_Anim);
    SetZIndex(5.0F);
    m_Transform.translation = {0.0F, 0.0F};
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
}
} // namespace Game
