#include "entities/Player.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "Util/Input.hpp"
#include "Util/Keycode.hpp"

namespace Game {
namespace {
// Project animation convention: cNN_0..3 = walk, cNN_4..7 = idle. Every playable
// hero (c01..c13) has at least 8 base frames, so [0,7] is always safe.
std::vector<std::string> Frames(const std::string &root, const std::string &charId,
                                int begin, int end) {
    std::vector<std::string> f;
    f.reserve(static_cast<std::size_t>(end - begin + 1));
    for (int i = begin; i <= end; ++i) {
        f.push_back(root + "/sprites/" + charId + "_" + std::to_string(i) + ".png");
    }
    return f;
}
constexpr float kPixelsPerSpeedUnit = 30.0F;
} // namespace

Player::Player(const CharacterDef &def, const std::string &resourceRoot,
               const std::string &charId)
    : m_Stats(CombatStats::FromCharacter(def)),
      m_Speed(def.speed * kPixelsPerSpeedUnit),
      m_WalkAnim(std::make_shared<Util::Animation>(
          Frames(resourceRoot, charId, 0, 3), true, 90, true, 0)),
      m_IdleAnim(std::make_shared<Util::Animation>(
          Frames(resourceRoot, charId, 4, 7), true, 150, true, 0)) {
    SetDrawable(m_IdleAnim);
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

    const bool moving = (dir.x != 0.0F || dir.y != 0.0F);
    if (moving) {
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        dir /= len;
        m_Transform.translation += dir * (m_Speed * dtMs / 1000.0F);
        if (dir.x < 0.0F) {
            m_FacingLeft = true;
        } else if (dir.x > 0.0F) {
            m_FacingLeft = false;
        }
    }

    // Swap walk<->idle only on state change (a fresh SetDrawable each frame would
    // restart the clip). Faithful to the working project's walk/idle split.
    if (moving != m_Moving) {
        SetDrawable(moving ? m_WalkAnim : m_IdleAnim);
        m_Moving = moving;
    }
    // Facing flip: sprite native scale is 1, so +/-1 mirrors horizontally.
    m_Transform.scale.x = m_FacingLeft ? -1.0F : 1.0F;
}
} // namespace Game
